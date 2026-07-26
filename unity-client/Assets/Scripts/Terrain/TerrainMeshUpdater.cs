using System;
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

        public float CellSize => cellSize;
        public float HeightScale => heightScale;
        public HeightmapData LastData { get; private set; }

        // 스냅샷(HEIGHTMAP)을 받아 메시에 반영할 때마다 알림 -- 진행률 UI가 구독해서
        // "지금까지 몇 개 받았는지" 표시하는 용도.
        public event Action<HeightmapData> HeightmapReceived;

        // 새 요청을 시작할 때 진행률 UI가 카운터를 리셋할 수 있도록 알림.
        public event Action RequestStarted;

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
            RequestStarted?.Invoke();

            // onHeightmap은 이번 요청 하나당 여러 번 불릴 수 있다 (코어가 침식을 조금씩
            // 여러 스냅샷으로 스트리밍하므로) -- 매번 그대로 메시에 반영하면 그게 곧
            // 침식이 진행되는 애니메이션이 된다.
            coreClient.RequestHeightmap(
                parameters,
                OnHeightmapReceived,
                onComplete: null,
                OnError);
        }

        private void OnHeightmapReceived(HeightmapData data)
        {
            LastData = data;
            TerrainMeshBuilder.Update(_mesh, data, cellSize, heightScale);
            HeightmapReceived?.Invoke(data);
        }

        private void OnError(string message)
        {
            Debug.LogError($"TerrainMeshUpdater: request failed - {message}");
        }
    }
}
