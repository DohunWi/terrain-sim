using System.Collections.Generic;
using TerrainSim.TerrainGen;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.InputSystem.UI;
using UnityEngine.UI;

namespace TerrainSim.UI
{
    // Builds a slider panel at runtime from ParamManifest (same manifest as
    // tools/tuner.html) and drives TerrainMeshUpdater. Two modes, since "tune a
    // parameter and see the result" and "watch the erosion animate" want opposite
    // pacing:
    //   - Animate OFF: slider changes auto-fire (150ms debounced, same pattern as
    //     tools/tuner.html's scheduleRender()), forcing snapshotCount=1 so the
    //     result comes back as fast as possible with no intermediate frames.
    //   - Animate ON: slider changes only update pending values; nothing is sent
    //     until Play is pressed, so nudging a slider mid-playback doesn't restart
    //     the animation.
    // CoreClient.IsBusy is checked throughout since request-response only allows
    // one in-flight request per connection (see CoreClient.RequestHeightmap's guard).
    public class TerrainControlPanel : MonoBehaviour
    {
        [SerializeField] private Net.CoreClient coreClient;
        [SerializeField] private TerrainMeshUpdater terrainMeshUpdater;

        private const float DebounceSeconds = 0.15f;

        private string _currentSim = "droplet";
        private readonly Dictionary<string, float> _values = new();
        private readonly List<GameObject> _rowObjects = new();

        private bool _dirty;
        private float _lastChangeTime;
        private bool _pendingExplicitRequest;

        private RectTransform _panelContent;
        private Font _font;
        private Text _progressText;
        private Button _animateButton;
        private int _snapshotsReceived;

        // Animate 꺼짐: 슬라이더 튜닝용 -- 값 바꿀 때마다(디바운스만 걸고) 자동으로
        // 빠른 결과(snapshotCount=1, 애니메이션 없음) 요청. Animate 켜짐: 슬라이더는
        // 값만 바꾸고, Play를 눌러야 그 값 그대로(실제 snapshotCount) 재생 요청을 보낸다
        // -- 슬라이더 하나 만질 때마다 재생 중이던 애니메이션이 끊기고 다시 시작되는
        // 걸 막기 위함.
        private bool _animate = true;

        private void Start()
        {
            EnsureEventSystem();
            _font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");

            BuildCanvas();
            ResetValuesToDefaults();
            RebuildParamRows();

            terrainMeshUpdater.RequestStarted += () =>
            {
                _snapshotsReceived = 0;
                UpdateProgressText();
            };
            terrainMeshUpdater.HeightmapReceived += _ =>
            {
                _snapshotsReceived++;
                UpdateProgressText();
            };

            // TerrainMeshUpdater.Start()가 coreClient.Connect()를 부르는데, Unity는
            // 서로 다른 GameObject의 Start() 호출 순서를 보장 안 해줌 -- 여기서 바로
            // SendRequest()하면 아직 연결 전일 수 있음. 연결될 때까지 기다렸다가 보낸다.
            _pendingExplicitRequest = true;
        }

        // main.cpp의 batches/steps 계산과 대응 -- 요청한 snapshotCount와 실제 예산
        // (numDroplets/iterations) 중 작은 쪽이 실제로 오는 HEIGHTMAP 개수.
        private int ExpectedSnapshotCount()
        {
            if (!_animate) return 1;
            int snapshotCount = Mathf.Max(1, (int)_values["snapshotCount"]);
            string budgetKey = _currentSim == "droplet" ? "numDroplets" : "iterations";
            int budget = Mathf.Max(1, (int)_values[budgetKey]);
            return Mathf.Min(snapshotCount, budget);
        }

        private void UpdateProgressText()
        {
            if (_progressText != null)
                _progressText.text = $"Snapshot {_snapshotsReceived} / {ExpectedSnapshotCount()}";
        }

        private void Update()
        {
            // Play/sim전환/초기 로딩처럼 명시적으로 요청한 것은 Animate 상태와 무관하게,
            // 연결이 되어 있고 지금 진행 중인 요청이 없는 대로 나간다.
            if (_pendingExplicitRequest && coreClient.IsConnected && !coreClient.IsBusy)
            {
                _pendingExplicitRequest = false;
                SendRequest();
            }

            // 슬라이더 드래그로 인한 자동 요청은 Animate 꺼짐(빠른 튜닝)일 때만.
            if (_animate) return;

            if (_dirty && Time.unscaledTime - _lastChangeTime >= DebounceSeconds && !coreClient.IsBusy)
            {
                _dirty = false;
                SendRequest();
            }
        }

        private void MarkDirty()
        {
            _dirty = true;
            _lastChangeTime = Time.unscaledTime;
        }

        // Play 버튼, sim 전환처럼 "지금 바로 재생해라"라는 명시적 요청.
        // 아직 연결 전이거나 이미 요청이 진행 중이면 예약만 해두고, Update()가
        // 조건이 맞는 대로 나간다.
        private void RequestNow()
        {
            if (!coreClient.IsConnected || coreClient.IsBusy) { _pendingExplicitRequest = true; return; }
            SendRequest();
        }

        private void ToggleAnimate()
        {
            _animate = !_animate;
            _animateButton.GetComponentInChildren<Text>().text = AnimateLabel();
            if (!_animate) MarkDirty(); // 튜닝 모드로 바뀌면 바로 한 번 갱신
        }

        private string AnimateLabel() => _animate ? "Animate: ON" : "Animate: OFF";

        private void SendRequest()
        {
            terrainMeshUpdater.RequestUpdate(BuildParamsDictionary());
        }

        private Dictionary<string, string> BuildParamsDictionary()
        {
            var result = new Dictionary<string, string> { ["sim"] = _currentSim };
            foreach (var kv in _values)
            {
                result[kv.Key] = kv.Value.ToString(System.Globalization.CultureInfo.InvariantCulture);
            }
            if (!_animate) result["snapshotCount"] = "1";
            return result;
        }

        private void ResetValuesToDefaults()
        {
            _values.Clear();
            foreach (var spec in ParamManifest.Shared) _values[spec.Name] = spec.Default;
            foreach (var spec in ParamManifest.Sims[_currentSim]) _values[spec.Name] = spec.Default;
        }

        private void SwitchSim(string sim)
        {
            if (_currentSim == sim) return;
            _currentSim = sim;
            ResetValuesToDefaults();
            RebuildParamRows();
            // sim 전환은 슬라이더 드래그와 달리 명시적인 액션이라, Animate 켜짐이어도
            // Play를 따로 안 눌러도 바로 재생한다.
            RequestNow();
        }

        // ---- UI construction ----

        private static void EnsureEventSystem()
        {
            if (FindAnyObjectByType<EventSystem>() != null) return;

            var go = new GameObject("EventSystem");
            go.AddComponent<EventSystem>();
            // 이 프로젝트는 새 Input System 전용(ProjectSettings의 activeInputHandler=1)이라
            // 레거시 StandaloneInputModule 대신 InputSystemUIInputModule이 필요함.
            go.AddComponent<InputSystemUIInputModule>();
        }

        private void BuildCanvas()
        {
            var canvasGO = new GameObject("TerrainControlCanvas");
            var canvas = canvasGO.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvasGO.AddComponent<CanvasScaler>();
            canvasGO.AddComponent<GraphicRaycaster>();

            var panelGO = new GameObject("Panel");
            panelGO.transform.SetParent(canvasGO.transform, false);
            var panelRect = panelGO.AddComponent<RectTransform>();
            panelRect.anchorMin = new Vector2(0, 1);
            panelRect.anchorMax = new Vector2(0, 1);
            panelRect.pivot = new Vector2(0, 1);
            panelRect.anchoredPosition = new Vector2(10, -10);
            panelRect.sizeDelta = new Vector2(300, 0); // 높이는 ContentSizeFitter가 계산

            var panelImage = panelGO.AddComponent<Image>();
            panelImage.color = new Color(0, 0, 0, 0.55f);

            var layout = panelGO.AddComponent<VerticalLayoutGroup>();
            layout.padding = new RectOffset(10, 10, 10, 10);
            layout.spacing = 4;
            layout.childControlHeight = true;
            layout.childControlWidth = true;
            layout.childForceExpandWidth = true;
            layout.childForceExpandHeight = false;

            // 패널 자신의 높이를 내용물(심 버튼 행 + 파라미터 행들) 합계에 맞춰 자동 계산.
            // sim 전환마다 행 개수가 바뀌므로(droplet 10개 vs thermal 3개) 고정 높이로는 안 맞음.
            var fitter = panelGO.AddComponent<ContentSizeFitter>();
            fitter.verticalFit = ContentSizeFitter.FitMode.PreferredSize;

            var simRow = CreateRow(panelRect, height: 30);
            CreateButton("Droplet", simRow, () => SwitchSim("droplet"));
            CreateButton("Thermal", simRow, () => SwitchSim("thermal"));

            var playbackRow = CreateRow(panelRect, height: 26);
            _animateButton = CreateButton(AnimateLabel(), playbackRow, ToggleAnimate);
            CreateButton("Play", playbackRow, RequestNow);

            var progressRow = CreateRow(panelRect, height: 20);
            _progressText = CreateText("Snapshot 0 / 0", progressRow, width: 280);

            var contentGO = new GameObject("ParamRows");
            contentGO.transform.SetParent(panelRect, false);
            _panelContent = contentGO.AddComponent<RectTransform>();
            var contentLayout = contentGO.AddComponent<VerticalLayoutGroup>();
            contentLayout.spacing = 2;
            contentLayout.childControlHeight = true;
            contentLayout.childControlWidth = true;
            contentLayout.childForceExpandWidth = true;
            contentLayout.childForceExpandHeight = false;
            var contentFitter = contentGO.AddComponent<ContentSizeFitter>();
            contentFitter.verticalFit = ContentSizeFitter.FitMode.PreferredSize;
        }

        private void RebuildParamRows()
        {
            foreach (var obj in _rowObjects) Destroy(obj);
            _rowObjects.Clear();

            foreach (var spec in ParamManifest.Shared) AddParamRow(spec);
            foreach (var spec in ParamManifest.Sims[_currentSim]) AddParamRow(spec);
        }

        private void AddParamRow(ParamSpec spec)
        {
            var row = CreateRow(_panelContent, height: 22);
            _rowObjects.Add(row.gameObject);

            var label = CreateText($"{spec.Label}", row, width: 120);
            var slider = CreateSlider(row, spec.Min, spec.Max, _values[spec.Name], width: 120);
            var valueText = CreateText(FormatValue(spec, _values[spec.Name]), row, width: 50);

            slider.onValueChanged.AddListener(v =>
            {
                float value = spec.IsInt ? Mathf.Round(v) : v;
                _values[spec.Name] = value;
                valueText.text = FormatValue(spec, value);
                MarkDirty();
            });
        }

        private static string FormatValue(ParamSpec spec, float value)
        {
            return spec.IsInt ? ((int)value).ToString() : value.ToString("0.###");
        }

        private RectTransform CreateRow(Transform parent, float height)
        {
            var go = new GameObject("Row");
            go.transform.SetParent(parent, false);
            var rect = go.AddComponent<RectTransform>();
            var layoutElement = go.AddComponent<LayoutElement>();
            layoutElement.preferredHeight = height;
            var layout = go.AddComponent<HorizontalLayoutGroup>();
            layout.spacing = 4;
            layout.childControlHeight = true;
            layout.childControlWidth = false;
            layout.childForceExpandHeight = true;
            return rect;
        }

        private Text CreateText(string content, Transform parent, float width)
        {
            var go = new GameObject("Text");
            go.transform.SetParent(parent, false);
            var layoutElement = go.AddComponent<LayoutElement>();
            layoutElement.preferredWidth = width;
            var text = go.AddComponent<Text>();
            text.font = _font;
            text.fontSize = 13;
            text.color = Color.white;
            text.text = content;
            text.alignment = TextAnchor.MiddleLeft;
            return text;
        }

        private Button CreateButton(string label, Transform parent, UnityEngine.Events.UnityAction onClick)
        {
            var go = new GameObject(label + "Button");
            go.transform.SetParent(parent, false);
            var layoutElement = go.AddComponent<LayoutElement>();
            layoutElement.preferredWidth = 130;
            layoutElement.preferredHeight = 26;

            var image = go.AddComponent<Image>();
            image.color = new Color(0.25f, 0.25f, 0.25f, 1f);

            var button = go.AddComponent<Button>();
            button.targetGraphic = image;
            button.onClick.AddListener(onClick);

            CreateText(label, go.transform, width: 130);
            return button;
        }

        private Slider CreateSlider(Transform parent, float min, float max, float value, float width)
        {
            var go = new GameObject("Slider");
            go.transform.SetParent(parent, false);
            var layoutElement = go.AddComponent<LayoutElement>();
            layoutElement.preferredWidth = width;
            var rect = go.GetComponent<RectTransform>();
            rect.sizeDelta = new Vector2(width, 20);

            var bgGO = new GameObject("Background");
            bgGO.transform.SetParent(go.transform, false);
            var bgRect = bgGO.AddComponent<RectTransform>();
            bgRect.anchorMin = new Vector2(0, 0.25f);
            bgRect.anchorMax = new Vector2(1, 0.75f);
            bgRect.sizeDelta = Vector2.zero;
            var bgImage = bgGO.AddComponent<Image>();
            bgImage.color = new Color(0.15f, 0.15f, 0.15f, 1f);

            var fillAreaGO = new GameObject("Fill Area");
            fillAreaGO.transform.SetParent(go.transform, false);
            var fillAreaRect = fillAreaGO.AddComponent<RectTransform>();
            fillAreaRect.anchorMin = new Vector2(0, 0.25f);
            fillAreaRect.anchorMax = new Vector2(1, 0.75f);
            fillAreaRect.sizeDelta = Vector2.zero;

            var fillGO = new GameObject("Fill");
            fillGO.transform.SetParent(fillAreaGO.transform, false);
            var fillRect = fillGO.AddComponent<RectTransform>();
            fillRect.sizeDelta = Vector2.zero;
            var fillImage = fillGO.AddComponent<Image>();
            fillImage.color = new Color(0.4f, 0.7f, 0.9f, 1f);

            var handleAreaGO = new GameObject("Handle Slide Area");
            handleAreaGO.transform.SetParent(go.transform, false);
            var handleAreaRect = handleAreaGO.AddComponent<RectTransform>();
            handleAreaRect.anchorMin = new Vector2(0, 0);
            handleAreaRect.anchorMax = new Vector2(1, 1);
            handleAreaRect.sizeDelta = new Vector2(-20, 0);

            var handleGO = new GameObject("Handle");
            handleGO.transform.SetParent(handleAreaGO.transform, false);
            var handleRect = handleGO.AddComponent<RectTransform>();
            handleRect.sizeDelta = new Vector2(16, 16);
            var handleImage = handleGO.AddComponent<Image>();
            handleImage.color = Color.white;

            var slider = go.AddComponent<Slider>();
            slider.targetGraphic = handleImage;
            slider.fillRect = fillRect;
            slider.handleRect = handleRect;
            slider.direction = Slider.Direction.LeftToRight;
            slider.minValue = min;
            slider.maxValue = max;
            slider.value = value;

            return slider;
        }
    }
}
