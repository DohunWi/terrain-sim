using TerrainSim.Net;
using UnityEngine;
using UnityEngine.Rendering;

namespace TerrainSim.TerrainGen
{
    // HeightmapData (row-major, y*Width+x -- matches core/src/heightmap.h's layout,
    // carried through unchanged by TerrainSim.Net) -> a Unity grid Mesh. Height
    // values map to world Y; X/Z spacing between grid points is cellSize.
    public static class TerrainMeshBuilder
    {
        public static Mesh Build(HeightmapData data, float cellSize, float heightScale)
        {
            var mesh = new Mesh { name = "TerrainMesh" };
            Update(mesh, data, cellSize, heightScale);
            return mesh;
        }

        // 기존 Mesh를 재사용해서 갱신 -- 슬라이더 조작마다 새 Mesh를 만들지 않고
        // 정점/삼각형 배열만 다시 채움.
        public static void Update(Mesh mesh, HeightmapData data, float cellSize, float heightScale)
        {
            int width = data.Width;
            int height = data.Height;

            // 65535개 넘는 정점(256x256 이상)을 다룰 수도 있으니 32비트 인덱스로 고정.
            mesh.indexFormat = IndexFormat.UInt32;

            var vertices = new Vector3[width * height];
            var uvs = new Vector2[width * height];
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    int i = y * width + x;
                    vertices[i] = new Vector3(x * cellSize, data.At(x, y) * heightScale, y * cellSize);
                    uvs[i] = new Vector2((float)x / (width - 1), (float)y / (height - 1));
                }
            }

            var triangles = new int[(width - 1) * (height - 1) * 6];
            int t = 0;
            for (int y = 0; y < height - 1; y++)
            {
                for (int x = 0; x < width - 1; x++)
                {
                    int a = y * width + x;
                    int b = y * width + x + 1;
                    int c = (y + 1) * width + x;
                    int d = (y + 1) * width + x + 1;

                    triangles[t++] = a;
                    triangles[t++] = c;
                    triangles[t++] = b;

                    triangles[t++] = b;
                    triangles[t++] = c;
                    triangles[t++] = d;
                }
            }

            mesh.Clear();
            mesh.vertices = vertices;
            mesh.uv = uvs;
            mesh.triangles = triangles;
            mesh.RecalculateNormals();
            mesh.RecalculateBounds();
        }
    }
}
