using System;
using System.IO;

namespace TerrainSim.Net
{
    public enum MessageType : byte
    {
        Params = 0x01,
        Heightmap = 0x02,
        Error = 0x03,
        HeightmapDone = 0x04,
    }

    public struct Envelope
    {
        public byte Type;
        public byte[] Payload;
    }

    // docs/net-protocol.md v1 envelope: [4-byte little-endian length][1-byte type][payload].
    // Mirrors core/src/net/protocol.{h,cpp} byte-for-byte -- same explicit shift/mask
    // assembly instead of a host-endian-dependent reinterpret, so this side doesn't
    // depend on the running machine's endianness matching the C++ core's.
    //
    // Stream.Write is guaranteed by the .NET Stream contract to write every requested
    // byte before returning, but Stream.Read can return fewer bytes than requested --
    // the same partial-read behavior as POSIX recv() on the C++ side -- so reads need
    // an explicit "loop until N bytes" helper (see core/src/net/io.cpp's recvAll).
    public static class NetProtocol
    {
        private const int HeaderSize = 5;

        public static void WriteEnvelope(Stream stream, byte type, byte[] payload)
        {
            byte[] header = new byte[HeaderSize];
            uint length = (uint)payload.Length;
            header[0] = (byte)(length & 0xFF);
            header[1] = (byte)((length >> 8) & 0xFF);
            header[2] = (byte)((length >> 16) & 0xFF);
            header[3] = (byte)((length >> 24) & 0xFF);
            header[4] = type;

            stream.Write(header, 0, header.Length);
            stream.Write(payload, 0, payload.Length);
        }

        public static Envelope ReadEnvelope(Stream stream)
        {
            byte[] header = new byte[HeaderSize];
            ReadExact(stream, header, HeaderSize);

            uint length = (uint)header[0]
                        | ((uint)header[1] << 8)
                        | ((uint)header[2] << 16)
                        | ((uint)header[3] << 24);
            byte type = header[4];

            byte[] payload = new byte[length];
            ReadExact(stream, payload, (int)length);

            return new Envelope { Type = type, Payload = payload };
        }

        private static void ReadExact(Stream stream, byte[] buffer, int count)
        {
            int done = 0;
            while (done < count)
            {
                int read = stream.Read(buffer, done, count - done);
                if (read == 0)
                    throw new IOException("connection closed while reading");
                done += read;
            }
        }

        // core/src/net/protocol.cpp가 std::bit_cast로 float 비트를 그대로 옮기는 것과
        // 대칭되는 디코딩 -- HeightmapData가 이걸 써서 float32 배열을 디코드한다.
        public static uint ReadUInt32LE(byte[] buf, int offset)
        {
            return (uint)buf[offset]
                 | ((uint)buf[offset + 1] << 8)
                 | ((uint)buf[offset + 2] << 16)
                 | ((uint)buf[offset + 3] << 24);
        }

        public static float ReadFloatLE(byte[] buf, int offset)
        {
            // BitConverter.ToSingle 대신: 이미 명시적으로 조립한 uint 비트를
            // 재해석하는 것뿐이라 호스트 엔디안(BitConverter.IsLittleEndian)에
            // 의존하지 않는다 -- core/src/net/protocol.cpp의 std::bit_cast와 대응.
            uint bits = ReadUInt32LE(buf, offset);
            return BitConverter.Int32BitsToSingle((int)bits);
        }
    }
}
