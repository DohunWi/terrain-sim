// EXP-008 (benchmarks/experiments/perf-thermal-erode-alloc.md) correctness
// gate: the std::vector -> std::array + reused-buffer refactor in
// core/src/erosion/thermal_erosion.cpp must produce bit-identical output to
// the pre-refactor implementation (erosion_reference.h/.cpp, a frozen copy),
// for the same input heightmap and parameters. thermalErode is the function
// TerrainAgentEnv.reset() calls in the frozen RL environment, so any cell-
// level difference here would invalidate existing evaluation results.
#include <gtest/gtest.h>

#include "../src/erosion/thermal_erosion.h"
#include "../src/heightmap.h"
#include "../src/noise/perlin_noise.h"
#include "erosion_reference.h"

namespace {

Heightmap generateFbmHeightmap(unsigned seed, int size = 64, float scale = 10.0f) {
    Heightmap hm(size, size);
    PerlinNoise noise;
    noise.reseed(seed);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            hm.at(x, y) = noise.fbm(static_cast<float>(x) / scale, static_cast<float>(y) / scale, 3, 0.5f, 2.0f);
        }
    }
    return hm;
}

}  // namespace

TEST(ThermalErodeRefactor, MatchesPreRefactorReferenceCellByCell) {
    constexpr int kNumSeeds = 20;
    constexpr float kTalusAngle = 0.15f;
    constexpr float kErosionRate = 0.3f;
    constexpr int kIterations = 10;

    for (unsigned seed = 0; seed < kNumSeeds; ++seed) {
        Heightmap actual = generateFbmHeightmap(seed);
        Heightmap expected = actual;  // same starting terrain for both

        thermalErode(actual, kTalusAngle, kErosionRate, kIterations);
        thermalErodeRef(expected, kTalusAngle, kErosionRate, kIterations);

        for (int y = 0; y < actual.height(); ++y) {
            for (int x = 0; x < actual.width(); ++x) {
                ASSERT_EQ(actual.at(x, y), expected.at(x, y))
                    << "seed=" << seed << " cell=(" << x << "," << y << ")";
            }
        }
    }
}

TEST(ThermalErodeRefactor, FindLowestNeighborMatchesReferencePerCell) {
    Heightmap hm = generateFbmHeightmap(/*seed=*/7);

    for (int y = 0; y < hm.height(); ++y) {
        for (int x = 0; x < hm.width(); ++x) {
            NeighborList actual = findLowestNeighbor(hm, x, y);
            std::vector<LowestNeighborRef> expected = findLowestNeighborRef(hm, x, y);

            ASSERT_EQ(actual.count, static_cast<int>(expected.size())) << "cell=(" << x << "," << y << ")";
            for (int i = 0; i < actual.count; ++i) {
                EXPECT_EQ(actual.items[i].x, expected[i].x);
                EXPECT_EQ(actual.items[i].y, expected[i].y);
                EXPECT_EQ(actual.items[i].value, expected[i].value);
                EXPECT_EQ(actual.items[i].diff, expected[i].diff);
            }
        }
    }
}
