using System;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using UnityEngine;

namespace TerrainSim.Net
{
    // TCP client for docs/net-protocol.md v1: a persistent connection to the C++
    // core (server, port 9000), one PARAMS -> HEIGHTMAP/ERROR request-response
    // cycle per RequestHeightmap() call.
    //
    // Network I/O runs on a background thread so it can't stall the Unity main
    // thread/editor; results are queued and drained in Update() since Unity API
    // calls (and the callbacks that touch scene objects) must happen on the main
    // thread.
    public class CoreClient : MonoBehaviour
    {
        [SerializeField] private string host = "127.0.0.1";
        [SerializeField] private int port = 9000;

        private TcpClient _client;
        private readonly object _lock = new();
        private readonly Queue<Action> _mainThreadQueue = new();

        public bool IsConnected => _client != null && _client.Connected;

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

        public void RequestHeightmap(Dictionary<string, string> parameters, Action<HeightmapData> onSuccess, Action<string> onError)
        {
            if (!IsConnected)
            {
                onError?.Invoke("not connected");
                return;
            }

            var stream = _client.GetStream();
            var payload = ParamsBuilder.Encode(parameters);

            new Thread(() =>
            {
                try
                {
                    NetProtocol.WriteEnvelope(stream, (byte)MessageType.Params, payload);
                    var response = NetProtocol.ReadEnvelope(stream);

                    if (response.Type == (byte)MessageType.Error)
                    {
                        string msg = Encoding.UTF8.GetString(response.Payload);
                        Enqueue(() => onError?.Invoke(msg));
                    }
                    else if (response.Type == (byte)MessageType.Heightmap)
                    {
                        var heightmap = HeightmapData.Decode(response.Payload);
                        Enqueue(() => onSuccess?.Invoke(heightmap));
                    }
                    else
                    {
                        Enqueue(() => onError?.Invoke($"unexpected response type {response.Type}"));
                    }
                }
                catch (Exception e)
                {
                    Enqueue(() => onError?.Invoke(e.Message));
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
            while (true)
            {
                Action action;
                lock (_lock)
                {
                    if (_mainThreadQueue.Count == 0) break;
                    action = _mainThreadQueue.Dequeue();
                }
                action();
            }
        }

        private void OnDestroy()
        {
            Disconnect();
        }
    }
}
