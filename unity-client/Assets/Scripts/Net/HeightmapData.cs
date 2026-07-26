namespace TerrainSim.Net
{
    // docs/net-protocol.md HEIGHTMAP payload:
    // [4-byte LE width][4-byte LE height][width*height*4-byte float32, row-major].
    // Values is flattened y*Width+x, matching core/src/heightmap.h's own layout.
    public class HeightmapData
    {
        public int Width;
        public int Height;
        public float[] Values;

        public float At(int x, int y) => Values[y * Width + x];

        public static HeightmapData Decode(byte[] payload)
        {
            uint width = NetProtocol.ReadUInt32LE(payload, 0);
            uint height = NetProtocol.ReadUInt32LE(payload, 4);

            var data = new HeightmapData
            {
                Width = (int)width,
                Height = (int)height,
                Values = new float[width * height],
            };

            int offset = 8;
            for (int i = 0; i < data.Values.Length; i++)
            {
                data.Values[i] = NetProtocol.ReadFloatLE(payload, offset);
                offset += 4;
            }

            return data;
        }
    }
}
