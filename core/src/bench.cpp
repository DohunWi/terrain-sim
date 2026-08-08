// Phase 2c performance instrumentation: pure C++ timing of the pieces the
// Gymnasium env calls every reset()/step() (see training/env.py), measured
// with no Python/pybind11 in the loop at all. This is the "native" baseline
// that training/bench_env.py's numbers get compared against to isolate the
// pybind11 call-boundary cost from the underlying C++ work.
//
// Not algorithm code -- a timing harness over existing core/ functions, no
// new simulation behavior (AGENTS.md's tests/benchmark/analysis carve-out).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "erosion/droplet_erosion.h"
#include "erosion/thermal_erosion.h"
#include "erosion/thermal_erosion_pull.h"
#include "heightmap.h"
#include "noise/perlin_noise.h"
#include "physics/rigid_body.h"
#include "physics/vec3.h"

namespace {

using Clock = std::chrono::steady_clock;

// Matches training/env.py's TerrainAgentEnv class defaults exactly, so this
// benchmark measures the same workload the RL loop actually pays for.
constexpr int kMapSize = 64;
constexpr float kScale = 10.0f;
constexpr int kOctaves = 3;
constexpr float kPersistence = 0.5f;
constexpr float kLacunarity = 2.0f;
constexpr float kTalusAngle = 0.15f;
constexpr float kErosionRate = 0.3f;
constexpr int kErosionIterations = 10;
constexpr float kDt = 1.0f / 60.0f;

struct Stats {
    double meanUs;
    double medianUs;
    double minUs;
    double maxUs;
};

Stats summarize(std::vector<double>& samplesUs) {
    std::sort(samplesUs.begin(), samplesUs.end());
    double sum = 0.0;
    for (double s : samplesUs) sum += s;
    Stats s;
    s.meanUs = sum / samplesUs.size();
    s.medianUs = samplesUs[samplesUs.size() / 2];
    s.minUs = samplesUs.front();
    s.maxUs = samplesUs.back();
    return s;
}

// Runs `fn` `iterations` times, discarding `warmup` runs first, returns one
// wall-clock duration (in microseconds) per timed iteration.
template <typename Fn>
std::vector<double> timeEach(int iterations, int warmup, Fn&& fn) {
    for (int i = 0; i < warmup; ++i) fn();
    std::vector<double> samples;
    samples.reserve(iterations);
    for (int i = 0; i < iterations; ++i) {
        auto start = Clock::now();
        fn();
        auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
    return samples;
}

Heightmap generateFbmHeightmap(unsigned seed) {
    Heightmap hm(kMapSize, kMapSize);
    PerlinNoise noise;
    noise.reseed(seed);
    for (int y = 0; y < kMapSize; ++y) {
        for (int x = 0; x < kMapSize; ++x) {
            hm.at(x, y) = noise.fbm(static_cast<float>(x) / kScale, static_cast<float>(y) / kScale,
                                     kOctaves, kPersistence, kLacunarity);
        }
    }
    return hm;
}

void printStat(const char* name, const Stats& s, int n) {
    std::printf("%-28s n=%-6d mean=%9.2f us  median=%9.2f us  min=%9.2f us  max=%9.2f us\n", name,
                n, s.meanUs, s.medianUs, s.minUs, s.maxUs);
}

struct Result {
    std::string name;
    int n;
    Stats stats;
};

void writeJson(const std::string& path, const std::vector<Result>& results) {
    std::ofstream out(path);
    out << "{\n  \"stages\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const Result& r = results[i];
        out << "    {\"name\": \"" << r.name << "\", \"n\": " << r.n << ", \"mean_us\": " << r.stats.meanUs
            << ", \"median_us\": " << r.stats.medianUs << ", \"min_us\": " << r.stats.minUs
            << ", \"max_us\": " << r.stats.maxUs << "}";
        out << (i + 1 < results.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string jsonOutPath = (argc > 1) ? argv[1] : "";
    std::vector<Result> results;
    constexpr int kIterations = 200;
    constexpr int kWarmup = 10;

    std::printf("terrain-sim C++ perf baseline (%dx%d heightmap, matches TerrainAgentEnv defaults)\n",
                kMapSize, kMapSize);
    std::printf("%-28s %-8s %-14s %-16s %-14s %-14s\n", "stage", "n", "mean_us", "median_us", "min_us",
                "max_us");

    // 1. Heightmap generation (fbm noise fill) -- reset()'s ts.generate_fbm_heightmap.
    unsigned seed = 0;
    auto fbmSamples = timeEach(kIterations, kWarmup, [&] {
        Heightmap hm = generateFbmHeightmap(seed++);
        // Force the result to actually be used (defeat dead-code elimination
        // of the fbm loop by reading one value out of it).
        volatile float sink = hm.at(0, 0);
        (void)sink;
    });
    auto fbmStats = summarize(fbmSamples);
    printStat("heightmap_generate_fbm", fbmStats, kIterations);
    results.push_back({"heightmap_generate_fbm", kIterations, fbmStats});

    // 2. Thermal erosion -- reset()'s ts.thermal_erode (TALUS_ANGLE=0.15, EROSION_RATE=0.3, 10 iters).
    seed = 0;
    auto thermalSamples = timeEach(kIterations, kWarmup, [&] {
        Heightmap hm = generateFbmHeightmap(seed++);
        thermalErode(hm, kTalusAngle, kErosionRate, kErosionIterations);
        volatile float sink = hm.at(0, 0);
        (void)sink;
    });
    auto thermalOnlySamples = std::vector<double>(thermalSamples.size());
    for (size_t i = 0; i < thermalSamples.size(); ++i) {
        thermalOnlySamples[i] = thermalSamples[i] - fbmStats.meanUs;
    }
    auto thermalOnlyStats = summarize(thermalOnlySamples);
    auto thermalCombinedStats = summarize(thermalSamples);
    printStat("thermal_erode_only(approx)", thermalOnlyStats, kIterations);
    printStat("fbm+thermal_erode(combined)", thermalCombinedStats, kIterations);
    results.push_back({"thermal_erode_only_approx", kIterations, thermalOnlyStats});
    results.push_back({"fbm_plus_thermal_erode_combined", kIterations, thermalCombinedStats});

    // 2b. Thermal erosion, pull-model variant (2d-2 item 5) -- same params as
    // above, same terrain-generation seeding, so this is a direct comparison
    // against stage 2's scatter-model numbers. Not on TerrainAgentEnv's
    // reset() path (core/tests/erosion_test.cpp's ThermalErodePullModel test
    // covers correctness within tolerance; not bit-exact vs. thermalErode).
    seed = 0;
    auto thermalPullSamples = timeEach(kIterations, kWarmup, [&] {
        Heightmap hm = generateFbmHeightmap(seed++);
        thermalErodePullModel(hm, kTalusAngle, kErosionRate, kErosionIterations);
        volatile float sink = hm.at(0, 0);
        (void)sink;
    });
    auto thermalPullOnlySamples = std::vector<double>(thermalPullSamples.size());
    for (size_t i = 0; i < thermalPullSamples.size(); ++i) {
        thermalPullOnlySamples[i] = thermalPullSamples[i] - fbmStats.meanUs;
    }
    auto thermalPullOnlyStats = summarize(thermalPullOnlySamples);
    auto thermalPullCombinedStats = summarize(thermalPullSamples);
    printStat("thermal_erode_pull_only(approx)", thermalPullOnlyStats, kIterations);
    printStat("fbm+thermal_erode_pull(combined)", thermalPullCombinedStats, kIterations);
    results.push_back({"thermal_erode_pull_only_approx", kIterations, thermalPullOnlyStats});
    results.push_back({"fbm_plus_thermal_erode_pull_combined", kIterations, thermalPullCombinedStats});

    // 3. Droplet erosion -- not on TerrainAgentEnv's reset() path today, but
    // part of the shared erosion/ stack (tune_cli.cpp, net protocol), so it's
    // measured too per AGENTS.md phase 2c item 2 ("erosion/reset").
    ErosionParams dropletParams{};
    dropletParams.inertia = 0.05f;
    dropletParams.minSlope = 0.01f;
    dropletParams.capacityFactor = 4.0f;
    dropletParams.erosionFactor = 0.3f;
    dropletParams.depositFactor = 0.3f;
    dropletParams.gravity = 9.8f;
    dropletParams.evaporateRate = 0.02f;
    dropletParams.waterThreshold = 0.01f;
    dropletParams.maxLifeTime = 30;
    seed = 0;
    auto dropletSamples = timeEach(kIterations, kWarmup, [&] {
        Heightmap hm = generateFbmHeightmap(seed);
        dropletErode(hm, dropletParams, /*numDroplets=*/700, seed);
        ++seed;
        volatile float sink = hm.at(0, 0);
        (void)sink;
    });
    auto dropletOnlySamples = std::vector<double>(dropletSamples.size());
    for (size_t i = 0; i < dropletSamples.size(); ++i) {
        dropletOnlySamples[i] = dropletSamples[i] - fbmStats.meanUs;
    }
    auto dropletOnlyStats = summarize(dropletOnlySamples);
    printStat("droplet_erode_only(approx,700)", dropletOnlyStats, kIterations);
    results.push_back({"droplet_erode_only_approx_700", kIterations, dropletOnlyStats});

    // 4. Rigid-body physics step -- step()'s ts.step_rigid_body, on a fixed
    // pre-generated terrain (matches how the env reuses one heightmap across
    // an episode's many steps).
    Heightmap physicsTerrain = generateFbmHeightmap(42);
    thermalErode(physicsTerrain, kTalusAngle, kErosionRate, kErosionIterations);
    RigidBody body;
    body.position = Vec3{32.0f, physicsTerrain.sample(32.0f, 32.0f).height + 5.0f, 32.0f};
    body.velocity = Vec3{0.0f, 0.0f, 0.0f};
    body.mass = 1.0f;
    Vec3 gravity{0.0f, -9.8f, 0.0f};
    Vec3 force{0.3f, 0.0f, 0.1f};
    constexpr int kPhysicsIterations = 100000;
    auto physicsSamples = timeEach(kPhysicsIterations, 1000, [&] {
        stepRigidBody(body, physicsTerrain, gravity, force, kDt);
        // Occasionally re-center so the body doesn't wander off the 64x64
        // map partway through the benchmark and start sampling extrapolated
        // (but now always-safe, see 2026-08-01's boundary fix) edge values.
        if (body.position.x < 4.0f || body.position.x > 60.0f || body.position.z < 4.0f ||
            body.position.z > 60.0f) {
            body.position = Vec3{32.0f, physicsTerrain.sample(32.0f, 32.0f).height + 5.0f, 32.0f};
            body.velocity = Vec3{0.0f, 0.0f, 0.0f};
        }
    });
    auto physicsStats = summarize(physicsSamples);
    printStat("rigid_body_step", physicsStats, kPhysicsIterations);
    results.push_back({"rigid_body_step", kPhysicsIterations, physicsStats});

    // 5. Heightmap::sample() alone -- the observation path's terrain query
    // (env.py's _observation() calls hm.sample(p.x, p.z) every step, on top
    // of whatever stepRigidBody already did internally).
    constexpr int kSampleIterations = 200000;
    float sx = 10.0f, sz = 10.0f;
    auto sampleSamples = timeEach(kSampleIterations, 1000, [&] {
        HeightSample hs = physicsTerrain.sample(sx, sz);
        sx += 0.013f;
        if (sx > 60.0f) sx = 4.0f;
        sz += 0.017f;
        if (sz > 60.0f) sz = 4.0f;
        volatile float sink = hs.height;
        (void)sink;
    });
    auto sampleStats = summarize(sampleSamples);
    printStat("heightmap_sample", sampleStats, kSampleIterations);
    results.push_back({"heightmap_sample", kSampleIterations, sampleStats});

    if (!jsonOutPath.empty()) {
        writeJson(jsonOutPath, results);
        std::printf("\nJSON written to %s\n", jsonOutPath.c_str());
    }

    return 0;
}
