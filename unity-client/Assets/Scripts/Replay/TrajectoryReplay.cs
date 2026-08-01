using System.IO;
using TerrainSim.Net;
using TerrainSim.TerrainGen;
using UnityEngine;

namespace TerrainSim.Replay
{
    // Loads Assets/StreamingAssets/<fileName> (written by training/eval.py
    // --dump-trajectory, one entry per evaluated episode) and lets you pick
    // which episode to replay from an on-screen list (OnGUI -- this is a dev/
    // demo tool, not shipped UI, so immediate-mode is enough). Selecting an
    // episode rebuilds the terrain mesh for that episode's heightmap (reusing
    // TerrainMeshBuilder, same cellSize/heightScale convention as the live TCP
    // path) and animates a marker along its recorded trajectory. No net/ or
    // C++ involvement -- static file load, separate from TerrainMeshUpdater's
    // live socket path.
    [RequireComponent(typeof(MeshFilter))]
    [RequireComponent(typeof(MeshRenderer))]
    public class TrajectoryReplay : MonoBehaviour
    {
        [SerializeField] private string fileName = "replay.json";
        [SerializeField] private float cellSize = 1.0f;
        [SerializeField] private float heightScale = 10.0f;
        [SerializeField] private float stepsPerSecond = 60.0f; // matches env.py's DT = 1/60
        [SerializeField] private float loopPauseSeconds = 1.5f;
        [SerializeField] private GameObject agentMarker;
        [SerializeField] private GameObject goalMarker;
        [SerializeField] private float agentMarkerScale = 2.0f;
        [SerializeField] private float goalMarkerScale = 1.0f;

        private MultiEpisodeReplay _data;
        private EpisodeRecord _current;
        private Mesh _mesh;
        private float _playbackTime;

        private void Start()
        {
            string path = Path.Combine(Application.streamingAssetsPath, fileName);
            if (!File.Exists(path))
            {
                Debug.LogError($"TrajectoryReplay: no file at {path} -- run training/eval.py --dump-trajectory first");
                enabled = false;
                return;
            }

            _data = JsonUtility.FromJson<MultiEpisodeReplay>(File.ReadAllText(path));
            _mesh = new Mesh { name = "ReplayTerrainMesh" };
            GetComponent<MeshFilter>().mesh = _mesh;

            if (agentMarker == null)
                agentMarker = CreateMarker("AgentMarker", Color.red, agentMarkerScale);
            if (goalMarker == null)
                goalMarker = CreateMarker("GoalMarker", Color.green, goalMarkerScale);

            if (_data.episodes != null && _data.episodes.Length > 0)
                SelectEpisode(_data.episodes[0]);
        }

        private void SelectEpisode(EpisodeRecord ep)
        {
            _current = ep;
            _playbackTime = 0f;

            var heightmapData = new HeightmapData { Width = ep.width, Height = ep.height, Values = ep.heightmap };
            TerrainMeshBuilder.Update(_mesh, heightmapData, cellSize, heightScale);

            float goalHeight = heightmapData.At(
                Mathf.Clamp(Mathf.RoundToInt(ep.goalX), 0, ep.width - 1),
                Mathf.Clamp(Mathf.RoundToInt(ep.goalZ), 0, ep.height - 1));
            goalMarker.transform.position = new Vector3(
                ep.goalX * cellSize, goalHeight * heightScale + 0.5f, ep.goalZ * cellSize);
        }

        private static GameObject CreateMarker(string name, Color color, float scale)
        {
            var go = GameObject.CreatePrimitive(PrimitiveType.Sphere);
            go.name = name;
            go.transform.localScale = Vector3.one * scale;
            go.GetComponent<Renderer>().material.color = color;
            Destroy(go.GetComponent<Collider>());
            return go;
        }

        private void Update()
        {
            if (_current == null || _current.trajectory == null || _current.trajectory.Length == 0)
                return;

            int lastIndex = _current.trajectory.Length - 1;
            float totalDuration = lastIndex / stepsPerSecond;

            _playbackTime += Time.deltaTime;
            float loopedTime = _playbackTime % (totalDuration + loopPauseSeconds);

            if (loopedTime > totalDuration)
            {
                agentMarker.transform.position = WorldPos(_current.trajectory[lastIndex]);
                return;
            }

            float frameFloat = loopedTime * stepsPerSecond;
            int i0 = Mathf.Clamp(Mathf.FloorToInt(frameFloat), 0, lastIndex);
            int i1 = Mathf.Min(i0 + 1, lastIndex);
            float t = frameFloat - i0;

            agentMarker.transform.position = Vector3.Lerp(
                WorldPos(_current.trajectory[i0]), WorldPos(_current.trajectory[i1]), t);
        }

        private Vector3 WorldPos(Vec3Data p) =>
            new Vector3(p.x * cellSize, p.y * heightScale, p.z * cellSize);

        private Vector2 _scroll;

        private void OnGUI()
        {
            if (_data?.episodes == null) return;

            const int width = 220;
            GUILayout.BeginArea(new Rect(10, 10, width, Screen.height - 20), GUI.skin.box);
            GUILayout.Label($"Episodes ({_data.episodes.Length})");
            _scroll = GUILayout.BeginScrollView(_scroll);
            foreach (var ep in _data.episodes)
            {
                var prevColor = GUI.color;
                GUI.color = ep.outcome switch
                {
                    "REACHED" => Color.green,
                    "OUT_OF_BOUNDS" => Color.red,
                    _ => Color.yellow, // TIMED_OUT
                };
                bool selected = _current == ep;
                string label = $"{(selected ? "> " : "  ")}#{ep.index:D2} {ep.outcome} ({ep.steps} steps)";
                if (GUILayout.Button(label))
                    SelectEpisode(ep);
                GUI.color = prevColor;
            }
            GUILayout.EndScrollView();
            GUILayout.EndArea();
        }
    }
}
