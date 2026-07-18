using System.Collections.Generic;
using TerrainSim.Net;
using UnityEngine;

namespace TerrainSim.TerrainGen
{
    // CoreClient(TCP)에서 받은 HeightmapData로 MeshFilter의 메시를 갱신한다.
    // 파라미터 슬라이더 UI가 생기면 RequestUpdate(Dictionary)를 그쪽에서 호출하면 됨 --
    // 보내지 않은 키는 core/src/main.cpp의 기본값(tune_cli.cpp와 동일 스키마)을 따른다.
    [RequireComponent(typeof(MeshFilter))]
    public class TerrainMeshUpdater : MonoBehaviour
    {
        [SerializeField] private CoreClient coreClient;
        [SerializeField] private float cellSize = 1.0f;
        [SerializeField] private float heightScale = 10.0f;

        [Header("기본 요청 파라미터 (docs/net-protocol.md 스키마)")]
        [SerializeField] private string sim = "droplet";
        [SerializeField] private int width = 64;
        [SerializeField] private int height = 64;

        private Mesh _mesh;

        private void Awake()
        {
            _mesh = new Mesh { name = "TerrainMesh" };
            GetComponent<MeshFilter>().mesh = _mesh;
        }

        private void Start()
        {
            if (!coreClient.IsConnected)
                coreClient.Connect();
            RequestUpdate();
        }

        public void RequestUpdate()
        {
            RequestUpdate(new Dictionary<string, string>
            {
                ["sim"] = sim,
                ["width"] = width.ToString(),
                ["height"] = height.ToString(),
            });
        }

        public void RequestUpdate(Dictionary<string, string> parameters)
        {
            coreClient.RequestHeightmap(parameters, OnHeightmapReceived, OnError);
        }

        private void OnHeightmapReceived(HeightmapData data)
        {
            TerrainMeshBuilder.Update(_mesh, data, cellSize, heightScale);
        }

        private void OnError(string message)
        {
            Debug.LogError($"TerrainMeshUpdater: request failed - {message}");
        }
    }
}
