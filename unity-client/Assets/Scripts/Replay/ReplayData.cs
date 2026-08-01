using System;

namespace TerrainSim.Replay
{
    // training/eval.py --dump-trajectory writes this shape directly (flat,
    // JsonUtility-friendly). heightmap is row-major (y*width+x), same
    // convention as TerrainSim.Net.HeightmapData / core/src/heightmap.h.
    [Serializable]
    public class Vec3Data
    {
        public float x;
        public float y;
        public float z;
    }

    [Serializable]
    public class EpisodeRecord
    {
        public int index;
        public string outcome; // "REACHED" | "OUT_OF_BOUNDS" | "TIMED_OUT"
        public int steps;
        public float finalDist;
        public int width;
        public int height;
        public float[] heightmap;
        public float goalX;
        public float goalZ;
        public Vec3Data[] trajectory;
    }

    [Serializable]
    public class MultiEpisodeReplay
    {
        public EpisodeRecord[] episodes;
    }
}
