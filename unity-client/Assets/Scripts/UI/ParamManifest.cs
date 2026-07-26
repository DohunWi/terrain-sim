using System.Collections.Generic;

namespace TerrainSim.UI
{
    public struct ParamSpec
    {
        public string Name;
        public string Label;
        public float Default;
        public float Min;
        public float Max;
        public bool IsInt;

        public ParamSpec(string name, string label, float def, float min, float max, bool isInt = false)
        {
            Name = name;
            Label = label;
            Default = def;
            Min = min;
            Max = max;
            IsInt = isInt;
        }
    }

    // Ported 1:1 from tools/tuner_server.py's SHARED_PARAMS/SIM_PARAMS -- same
    // names/ranges/defaults as the web tuner and core/src/tune_cli.cpp's CLI
    // defaults. Keep all three in sync if a param is added/renamed.
    public static class ParamManifest
    {
        public static readonly ParamSpec[] Shared =
        {
            new("width", "Width", 64, 16, 128, isInt: true),
            new("height", "Height", 64, 16, 128, isInt: true),
            new("terrainSeed", "Terrain seed", 42, 0, 1000, isInt: true),
            new("scale", "Noise scale", 10.0f, 1, 50),
            new("octaves", "Octaves", 3, 1, 8, isInt: true),
            new("persistence", "Persistence", 0.5f, 0, 1),
            new("lacunarity", "Lacunarity", 2.0f, 1, 4),
            new("snapshotCount", "Snapshot count", 12, 1, 30, isInt: true),
        };

        public static readonly Dictionary<string, ParamSpec[]> Sims = new()
        {
            ["droplet"] = new[]
            {
                new ParamSpec("numDroplets", "Num droplets", 700, 100, 8000, isInt: true),
                new ParamSpec("inertia", "Inertia", 0.3f, 0, 1),
                new ParamSpec("minSlope", "Min slope", 0.01f, 0, 0.2f),
                new ParamSpec("capacityFactor", "Capacity factor", 4.0f, 0.1f, 10),
                new ParamSpec("erosionFactor", "Erosion factor", 0.3f, 0, 1),
                new ParamSpec("depositFactor", "Deposit factor", 0.3f, 0, 1),
                new ParamSpec("gravity", "Gravity", 4.0f, 0, 20),
                new ParamSpec("evaporateRate", "Evaporate rate", 0.02f, 0, 0.5f),
                new ParamSpec("waterThreshold", "Water threshold", 0.01f, 0, 0.5f),
                new ParamSpec("maxLifeTime", "Max lifetime (steps)", 25, 1, 200, isInt: true),
            },
            ["thermal"] = new[]
            {
                new ParamSpec("talusAngle", "Talus angle", 0.1f, 0, 2),
                new ParamSpec("erosionRate", "Erosion rate", 0.3f, 0, 1),
                new ParamSpec("iterations", "Iterations", 10, 1, 100, isInt: true),
            },
        };
    }
}
