using System.Collections.Generic;
using System.Text;

namespace TerrainSim.Net
{
    // docs/net-protocol.md PARAMS payload: UTF-8 text, one "key=value" per line,
    // newline-separated. Field names match core/src/tune_cli.cpp and
    // tools/tuner_server.py's SIM_PARAMS by convention -- keep them in sync.
    public static class ParamsBuilder
    {
        public static byte[] Encode(Dictionary<string, string> parameters)
        {
            var sb = new StringBuilder();
            foreach (var kv in parameters)
            {
                sb.Append(kv.Key).Append('=').Append(kv.Value).Append('\n');
            }
            return Encoding.UTF8.GetBytes(sb.ToString());
        }
    }
}
