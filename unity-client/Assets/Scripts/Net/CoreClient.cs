using System;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;

namespace TerrainSim.Net
{
    // TCP client for docs/net-protocol.md v1: a persistent connection to the C++
    // core (server, port 9000). One RequestHeightmap() call sends one PARAMS and
    // then reads a *stream* of responses for that request: zero or more HEIGHTMAP
    // frames (onHeightmap fires once per frame -- the core sends incremental
    // erosion snapshots, not just a single final result), terminated by either
    // HEIGHTMAP_DONE (onComplete) or ERROR (onError).
    //
    // Network I/O runs on a background thread so it can't stall the Unity main
    // thread/editor; results are queued and drained in Update() since Unity API
    // calls (and the callbacks that touch scene objects) must happen on the main
    // thread.
    public class CoreClient : MonoBehaviour
    {
        [SerializeField] private string host = "127.0.0.1";
        [SerializeField] private int port = 9000;

        // HEIGHTMAP 스냅샷을 프레임당 여러 개씩 큐에서 비워도 그중 마지막 하나만
        // 렌더링되고 나머지는 그냥 덮어써져서 애니메이션이 순간이동처럼 보인다
        // (프레임 수 제한으론 못 고침 -- 프레임 안에서 몇 개를 처리하든 그 프레임엔
        // 한 번만 그려지므로). 대신 실시간 간격 기준으로 프레임당 최대 하나만
        // 처리해서, 재생 속도가 프레임레이트와 무관하게 일정하게 유지되도록 한다.
        [SerializeField] private float secondsPerCallback = 0.05f;

        private TcpClient _client;
        private readonly object _lock = new();
        private readonly Queue<Action> _mainThreadQueue = new();
        private bool _requestInFlight;
        private float _lastCallbackTime = float.NegativeInfinity;

        public bool IsConnected => _client != null && _client.Connected;

        // 요청이 이미 진행 중일 때 호출하면 무시된다 -- envelope는 request-response
        // 하나씩만 순서대로 오가는 게 전제라, 같은 연결(스트림)에 두 스레드가 동시에
        // write/read하면 프레이밍이 깨진다. 슬라이더 UI는 이 값을 보고 스로틀링한다.
        public bool IsBusy => _requestInFlight;

        public void Connect()
        {
            if (IsConnected) return;
            _client = new TcpClient();
            _client.Connect(host, port);
            Debug.Log($"CoreClient: connected to {host}:{port}");
        }

        public void Disconnect()
        {
            _client?.Close();
            _client = null;
        }

        public void RequestHeightmap(Dictionary<string, string> parameters, Action<HeightmapData> onHeightmap, Action onComplete, Action<string> onError)
        {
            if (!IsConnected)
            {
                onError?.Invoke("not connected");
                return;
            }

            lock (_lock)
            {
                if (_requestInFlight) return;
                _requestInFlight = true;
            }

            var stream = _client.GetStream();
            var payload = ParamsBuilder.Encode(parameters);

            new Thread(() =>
            {
                try
                {
                    NetProtocol.WriteEnvelope(stream, (byte)MessageType.Params, payload);

                    while (true)
                    {
                        var response = NetProtocol.ReadEnvelope(stream);

                        if (response.Type == (byte)MessageType.Heightmap)
                        {
                            var heightmap = HeightmapData.Decode(response.Payload);
                            Enqueue(() => onHeightmap?.Invoke(heightmap));
                            continue; // 이 요청의 스트림이 아직 안 끝남, 다음 프레임 계속 읽기
                        }

                        if (response.Type == (byte)MessageType.HeightmapDone)
                        {
                            Enqueue(() => onComplete?.Invoke());
                        }
                        else if (response.Type == (byte)MessageType.Error)
                        {
                            string msg = Encoding.UTF8.GetString(response.Payload);
                            Enqueue(() => onError?.Invoke(msg));
                        }
                        else
                        {
                            Enqueue(() => onError?.Invoke($"unexpected response type {response.Type}"));
                        }
                        break; // HEIGHTMAP_DONE/ERROR/그 외 -- 이 요청의 스트림은 여기서 끝
                    }
                }
                catch (Exception e)
                {
                    Enqueue(() => onError?.Invoke(e.Message));
                }
                finally
                {
                    lock (_lock) { _requestInFlight = false; }
                }
            }).Start();
        }

        private void Enqueue(Action action)
        {
            lock (_lock)
            {
                _mainThreadQueue.Enqueue(action);
            }
        }

        private void Update()
        {
            if (Time.unscaledTime - _lastCallbackTime < secondsPerCallback) return;

            Action action;
            lock (_lock)
            {
                if (_mainThreadQueue.Count == 0) return;
                action = _mainThreadQueue.Dequeue();
            }
            _lastCallbackTime = Time.unscaledTime;
            action();
        }

        private void OnDestroy()
        {
            Disconnect();
        }
    }
}
