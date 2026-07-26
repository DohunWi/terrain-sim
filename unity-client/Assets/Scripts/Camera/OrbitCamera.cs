using TerrainSim.Net;
using TerrainSim.TerrainGen;
using UnityEngine;
using UnityEngine.InputSystem;

namespace TerrainSim.CameraControl
{
    // Demo-capture orbit camera: hold the right mouse button to orbit around
    // `target`, scroll to zoom. Uses Mouse.current (new Input System) rather
    // than legacy UnityEngine.Input -- this project's ProjectSettings has
    // activeInputHandler=1 (Input System package only), so the old Input
    // class isn't available at runtime.
    public class OrbitCamera : MonoBehaviour
    {
        [SerializeField] private TerrainMeshUpdater terrainMeshUpdater;
        [SerializeField] private Vector3 target = new(32, 0, 32); // 기본 64x64 지형 중심, width/height 슬라이더로 지형 크기가 바뀌면 재계산됨
        [SerializeField] private float distance = 80f;
        [SerializeField] private float minDistance = 10f;
        [SerializeField] private float maxDistance = 300f;
        [SerializeField] private float rotationSpeed = 0.2f;
        [SerializeField] private float zoomSpeed = 5f;

        private float _yaw = 45f;
        private float _pitch = 35f;
        private int _lastWidth = -1;
        private int _lastHeight = -1;

        private void OnEnable()
        {
            if (terrainMeshUpdater != null)
                terrainMeshUpdater.HeightmapReceived += OnHeightmapReceived;
        }

        private void OnDisable()
        {
            if (terrainMeshUpdater != null)
                terrainMeshUpdater.HeightmapReceived -= OnHeightmapReceived;
        }

        // width/height 슬라이더로 지형 크기가 바뀌면 그만큼 target도 다시 중앙으로 맞춘다.
        // 매 스냅샷마다 재계산하면 애니메이션 재생 중 카메라가 계속 튀므로, 크기가
        // 실제로 바뀐 요청에서만(Width/Height가 이전 값과 다를 때만) 갱신한다.
        private void OnHeightmapReceived(HeightmapData data)
        {
            if (data.Width == _lastWidth && data.Height == _lastHeight) return;
            _lastWidth = data.Width;
            _lastHeight = data.Height;
            float cellSize = terrainMeshUpdater.CellSize;
            target = new Vector3((data.Width - 1) * cellSize / 2f, 0, (data.Height - 1) * cellSize / 2f);
        }

        private void LateUpdate()
        {
            var mouse = Mouse.current;
            if (mouse == null) return;

            if (mouse.rightButton.isPressed)
            {
                Vector2 delta = mouse.delta.ReadValue();
                _yaw += delta.x * rotationSpeed;
                _pitch -= delta.y * rotationSpeed;
                _pitch = Mathf.Clamp(_pitch, -10f, 89f);
            }

            float scroll = mouse.scroll.ReadValue().y;
            distance = Mathf.Clamp(distance - scroll * zoomSpeed * 0.01f, minDistance, maxDistance);

            var rotation = Quaternion.Euler(_pitch, _yaw, 0);
            transform.SetPositionAndRotation(target + rotation * new Vector3(0, 0, -distance), rotation);
        }
    }
}
