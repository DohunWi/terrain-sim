using System;

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
            uint width = ReadUInt32LE(payload, 0);
            uint height = ReadUInt32LE(payload, 4);

            var data = new HeightmapData
            {
                Width = (int)width,
                Height = (int)height,
                Values = new float[width * height],
            };

            int offset = 8;
            for (int i = 0; i < data.Values.Length; i++)
            {
                uint bits = ReadUInt32LE(payload, offset);
                // BitConverter.ToSingle would depend on BitConverter.IsLittleEndian
                // (the running platform's own endianness); Int32BitsToSingle instead
                // just reinterprets bits we've already assembled explicitly above,
                // so this doesn't depend on the host's endianness at all -- same
                // reasoning as core/src/net/protocol.cpp's std::bit_cast usage.
                data.Values[i] = BitConverter.Int32BitsToSingle((int)bits);
                offset += 4;
            }

            return data;
        }

        private static uint ReadUInt32LE(byte[] buf, int offset)
        {
            return (uint)buf[offset]
                 | ((uint)buf[offset + 1] << 8)
                 | ((uint)buf[offset + 2] << 16)
                 | ((uint)buf[offset + 3] << 24);
        }
    }
}
