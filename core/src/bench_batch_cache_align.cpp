// EXP-005 (benchmarks/experiments/perf-envslot-cache-align.md): does padding
// EnvSlot to a 64-byte cache-line boundary fix the sub-4x scaling observed in
// EXP-003 (benchmarks/experiments/perf-parallel-envs.md), and does it explain
// why EXP-004's PerlinNoise-reuse candidate got *worse* instead of better
// (benchmarks/experiments/perf-perlin-table-reuse.md)?
//
// Same std::barrier persistent-worker-pool harness as bench_batch.cpp /
// bench_batch_perlin_reuse.cpp. The one new variable here is EnvSlot's
// alignment/padding inside the contiguous std::vector<EnvSlot> the pool
// holds -- everything else (PerlinNoise allocated fresh per reset, matching
// EXP-003's baseline policy, not EXP-004's rejected reuse candidate) is held
// fixed. Mechanical benchmark/orchestration code, not physics/erosion
// algorithm work.
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "erosion/thermal_erosion.h"
#include "heightmap.h"
#include "noise/perlin_noise.h"
#include "physics/rigid_body.h"
#include "physics/vec3.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMapSize = 64;
constexpr float kScale = 10.0f;
constexpr int kOctaves = 3;
constexpr float kPersistence = 0.5f;
constexpr float kLacunarity = 2.0f;
constexpr float kTalusAngle = 0.15f;
constexpr float kErosionRate = 0.3f;
constexpr int kErosionIterations = 10;
constexpr float kDt = 1.0f / 60.0f;
constexpr int kStepsPerEpisode = 1000;

// Baseline: same layout as EXP-003's EnvSlot, no alignment directive --
// whatever the compiler's default struct layout/packing gives it.
struct EnvSlotDefault {
    Heightmap terrain{kMapSize, kMapSize};
    RigidBody body{};
};

// Candidate: forces this type's alignment (and therefore its size, which
// must be a multiple of its alignment) to a 64-byte cache line, so no two
// slots in a contiguous std::vector<EnvSlotAligned> can ever share a cache
// line with each other.
struct alignas(64) EnvSlotAligned {
    Heightmap terrain{kMapSize, kMapSize};
    RigidBody body{};
};

struct EnvResult {
    float posX, posY, posZ;
    float velX, velY, velZ;
    double heightmapChecksum;

    bool operator==(const EnvResult& o) const {
        return posX == o.posX && posY == o.posY && posZ == o.posZ && velX == o.velX &&
               velY == o.velY && velZ == o.velZ && heightmapChecksum == o.heightmapChecksum;
    }
};

double checksum(const Heightmap& hm) {
    double sum = 0.0;
    for (int y = 0; y < hm.height(); ++y) {
        for (int x = 0; x < hm.width(); ++x) sum += hm.at(x, y);
    }
    return sum;
}

template <typename Slot>
void resetSlot(Slot& slot, unsigned seed) {
    Heightmap hm(kMapSize, kMapSize);
    PerlinNoise noise;
    noise.reseed(seed);
    for (int y = 0; y < kMapSize; ++y) {
        for (int x = 0; x < kMapSize; ++x) {
            hm.at(x, y) = noise.fbm(static_cast<float>(x) / kScale, static_cast<float>(y) / kScale,
                                     kOctaves, kPersistence, kLacunarity);
        }
    }
    thermalErode(hm, kTalusAngle, kErosionRate, kErosionIterations);
    slot.terrain = std::move(hm);
    HeightSample start = slot.terrain.sample(32.0f, 32.0f);
    slot.body.position = Vec3{32.0f, start.height + 5.0f, 32.0f};
    slot.body.velocity = Vec3{0.0f, 0.0f, 0.0f};
    slot.body.mass = 1.0f;
}

template <typename Slot>
void stepSlot(Slot& slot, int numSteps) {
    Vec3 gravity{0.0f, -9.8f, 0.0f};
    Vec3 force{0.3f, 0.0f, 0.1f};
    for (int i = 0; i < numSteps; ++i) {
        stepRigidBody(slot.body, slot.terrain, gravity, force, kDt);
        if (slot.body.position.x < 4.0f || slot.body.position.x > 60.0f ||
            slot.body.position.z < 4.0f || slot.body.position.z > 60.0f) {
            HeightSample start = slot.terrain.sample(32.0f, 32.0f);
            slot.body.position = Vec3{32.0f, start.height + 5.0f, 32.0f};
            slot.body.velocity = Vec3{0.0f, 0.0f, 0.0f};
        }
    }
}

template <typename Slot>
EnvResult resultOf(const Slot& slot) {
    return EnvResult{slot.body.position.x, slot.body.position.y, slot.body.position.z,
                      slot.body.velocity.x, slot.body.velocity.y, slot.body.velocity.z,
                      checksum(slot.terrain)};
}

template <typename Slot>
std::vector<EnvResult> runSequential(int numEnvs, int stepsPerEnv) {
    std::vector<EnvResult> results(numEnvs);
    for (int i = 0; i < numEnvs; ++i) {
        Slot slot;
        resetSlot(slot, static_cast<unsigned>(i));
        stepSlot(slot, stepsPerEnv);
        results[i] = resultOf(slot);
    }
    return results;
}

enum class Phase { Reset, Step, Stop };

template <typename Slot>
class WorkerPool {
public:
    WorkerPool(int threadCount, int stepsPerEnv)
        : slots_(threadCount),
          stepsPerEnv_(stepsPerEnv),
          startBarrier_(threadCount + 1),
          endBarrier_(threadCount + 1) {
        threads_.reserve(threadCount);
        for (int i = 0; i < threadCount; ++i) {
            threads_.emplace_back([this, i] { workerLoop(i); });
        }
    }

    ~WorkerPool() {
        phase_ = Phase::Stop;
        startBarrier_.arrive_and_wait();
        for (auto& t : threads_) t.join();
    }

    double runPhase(Phase p) {
        phase_ = p;
        auto start = Clock::now();
        startBarrier_.arrive_and_wait();
        endBarrier_.arrive_and_wait();
        auto end = Clock::now();
        return std::chrono::duration<double>(end - start).count();
    }

    std::vector<EnvResult> collectResults() {
        std::vector<EnvResult> results(slots_.size());
        for (size_t i = 0; i < slots_.size(); ++i) results[i] = resultOf(slots_[i]);
        return results;
    }

private:
    void workerLoop(int i) {
        for (;;) {
            startBarrier_.arrive_and_wait();
            Phase p = phase_;
            if (p == Phase::Stop) return;
            if (p == Phase::Reset) {
                resetSlot(slots_[i], static_cast<unsigned>(i));
            } else {
                stepSlot(slots_[i], stepsPerEnv_);
            }
            endBarrier_.arrive_and_wait();
        }
    }

    std::vector<Slot> slots_;
    int stepsPerEnv_;
    std::atomic<Phase> phase_{Phase::Reset};
    std::barrier<> startBarrier_;
    std::barrier<> endBarrier_;
    std::vector<std::thread> threads_;
};

struct SweepPoint {
    int threadCount;
    double resetsPerSecMean, resetsPerSecSd;
    double stepsPerSecMean, stepsPerSecSd;
};

double mean(const std::vector<double>& v) {
    double s = 0.0;
    for (double x : v) s += x;
    return s / v.size();
}

double stddev(const std::vector<double>& v, double m) {
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / v.size());
}

template <typename Slot>
std::vector<SweepPoint> runSweep(const std::vector<int>& threadCounts, int repeats,
                                  int episodesPerStepPhase) {
    std::vector<SweepPoint> sweep;
    for (int tc : threadCounts) {
        WorkerPool<Slot> pool(tc, episodesPerStepPhase * kStepsPerEpisode);
        std::vector<double> resetsPerSec, stepsPerSec;
        for (int r = 0; r < repeats; ++r) {
            double resetSeconds = pool.runPhase(Phase::Reset);
            resetsPerSec.push_back(tc / resetSeconds);
            double stepSeconds = pool.runPhase(Phase::Step);
            stepsPerSec.push_back((static_cast<double>(tc) * episodesPerStepPhase * kStepsPerEpisode) /
                                   stepSeconds);
        }
        double rMean = mean(resetsPerSec), rSd = stddev(resetsPerSec, rMean);
        double sMean = mean(stepsPerSec), sSd = stddev(stepsPerSec, sMean);
        sweep.push_back({tc, rMean, rSd, sMean, sSd});
    }
    return sweep;
}

}  // namespace

int main(int argc, char** argv) {
    std::string jsonOutPath = (argc > 1) ? argv[1] : "";

    std::printf("sizeof(EnvSlotDefault) = %zu, alignof = %zu\n", sizeof(EnvSlotDefault),
                alignof(EnvSlotDefault));
    std::printf("sizeof(EnvSlotAligned) = %zu, alignof = %zu\n\n", sizeof(EnvSlotAligned),
                alignof(EnvSlotAligned));

    // --- Correctness gate: parallel == sequential, for both layouts. ---
    constexpr int kCorrectnessEnvs = 8;
    constexpr int kCorrectnessSteps = 200;
    bool identical = true;

    {
        auto seq = runSequential<EnvSlotDefault>(kCorrectnessEnvs, kCorrectnessSteps);
        WorkerPool<EnvSlotDefault> pool(kCorrectnessEnvs, kCorrectnessSteps);
        pool.runPhase(Phase::Reset);
        pool.runPhase(Phase::Step);
        auto par = pool.collectResults();
        for (int i = 0; i < kCorrectnessEnvs; ++i) {
            if (!(seq[i] == par[i])) {
                identical = false;
                std::printf("MISMATCH (default layout) at env %d\n", i);
            }
        }
    }
    {
        auto seq = runSequential<EnvSlotAligned>(kCorrectnessEnvs, kCorrectnessSteps);
        WorkerPool<EnvSlotAligned> pool(kCorrectnessEnvs, kCorrectnessSteps);
        pool.runPhase(Phase::Reset);
        pool.runPhase(Phase::Step);
        auto par = pool.collectResults();
        for (int i = 0; i < kCorrectnessEnvs; ++i) {
            if (!(seq[i] == par[i])) {
                identical = false;
                std::printf("MISMATCH (aligned layout) at env %d\n", i);
            }
        }
    }
    std::printf("Correctness check (parallel == sequential, both layouts): %s\n\n",
                identical ? "PASS" : "FAIL");
    if (!identical) {
        std::printf("Aborting throughput sweep -- correctness gate failed "
                    "(perf-envslot-cache-align.md: Rejected regardless of speed).\n");
        return 1;
    }

    // --- Throughput sweep, default vs cache-aligned layout. ---
    const std::vector<int> threadCounts = {1, 2, 4, 6, 8, 12, 16};
    // Bumped 7 -> 20 (EXP-007/008 lesson: n=7 wasn't enough to reliably
    // separate real effects from noise for this workload).
    constexpr int kRepeats = 20;
    constexpr int kEpisodesPerStepPhase = 50;

    auto defaultSweep = runSweep<EnvSlotDefault>(threadCounts, kRepeats, kEpisodesPerStepPhase);
    auto alignedSweep = runSweep<EnvSlotAligned>(threadCounts, kRepeats, kEpisodesPerStepPhase);

    std::printf("%-10s %-26s %-26s %-24s %-24s\n", "threads", "resets/s default(mean+-sd)",
                "resets/s aligned(mean+-sd)", "steps/s default(mean+-sd)",
                "steps/s aligned(mean+-sd)");
    double baselineStepsPerSec1Thread = defaultSweep[0].stepsPerSecMean;
    double baselineResetsPerSec1Thread = defaultSweep[0].resetsPerSecMean;
    for (size_t i = 0; i < threadCounts.size(); ++i) {
        const auto& d = defaultSweep[i];
        const auto& a = alignedSweep[i];
        std::printf("%-10d %8.1f +- %-8.1f %8.1f +- %-8.1f %10.1f +- %-8.1f %10.1f +- %-8.1f\n",
                    threadCounts[i], d.resetsPerSecMean, d.resetsPerSecSd, a.resetsPerSecMean,
                    a.resetsPerSecSd, d.stepsPerSecMean, d.stepsPerSecSd, a.stepsPerSecMean,
                    a.stepsPerSecSd);
    }

    auto it8 = std::find(threadCounts.begin(), threadCounts.end(), 8);
    if (it8 != threadCounts.end()) {
        size_t idx8 = static_cast<size_t>(it8 - threadCounts.begin());
        double alignedResetsSpeedup = alignedSweep[idx8].resetsPerSecMean / baselineResetsPerSec1Thread;
        double alignedStepsSpeedup = alignedSweep[idx8].stepsPerSecMean / baselineStepsPerSec1Thread;
        double resetsImprovementPct =
            100.0 * (alignedSweep[idx8].resetsPerSecMean / defaultSweep[idx8].resetsPerSecMean - 1.0);
        double stepsImprovementPct =
            100.0 * (alignedSweep[idx8].stepsPerSecMean / defaultSweep[idx8].stepsPerSecMean - 1.0);
        std::printf("\nAt thread_count=8: aligned speedup vs its own 1-thread = resets %.2fx / steps %.2fx\n",
                    alignedResetsSpeedup, alignedStepsSpeedup);
        std::printf("At thread_count=8: aligned vs default (this experiment's baseline) improvement = "
                    "resets %.1f%% / steps %.1f%%\n",
                    resetsImprovementPct, stepsImprovementPct);
        const char* verdict = "Inconclusive";
        if (alignedResetsSpeedup >= 4.0 && alignedStepsSpeedup >= 4.0 && resetsImprovementPct >= 30.0 &&
            stepsImprovementPct >= 30.0) {
            verdict = "Accepted";
        } else if (resetsImprovementPct < 15.0 && stepsImprovementPct < 15.0) {
            verdict = "Rejected";
        }
        std::printf("Verdict against perf-envslot-cache-align.md's pre-registered criteria: %s\n",
                    verdict);
    }

    if (!jsonOutPath.empty()) {
        std::ofstream out(jsonOutPath);
        out << "{\n  \"correctness_check_pass\": true,\n"
            << "  \"sizeof_default\": " << sizeof(EnvSlotDefault) << ",\n"
            << "  \"sizeof_aligned\": " << sizeof(EnvSlotAligned) << ",\n"
            << "  \"sweep\": [\n";
        for (size_t i = 0; i < threadCounts.size(); ++i) {
            const auto& d = defaultSweep[i];
            const auto& a = alignedSweep[i];
            out << "    {\"thread_count\": " << threadCounts[i]
                << ", \"resets_per_sec_default_mean\": " << d.resetsPerSecMean
                << ", \"resets_per_sec_default_sd\": " << d.resetsPerSecSd
                << ", \"resets_per_sec_aligned_mean\": " << a.resetsPerSecMean
                << ", \"resets_per_sec_aligned_sd\": " << a.resetsPerSecSd
                << ", \"steps_per_sec_default_mean\": " << d.stepsPerSecMean
                << ", \"steps_per_sec_default_sd\": " << d.stepsPerSecSd
                << ", \"steps_per_sec_aligned_mean\": " << a.stepsPerSecMean
                << ", \"steps_per_sec_aligned_sd\": " << a.stepsPerSecSd << "}";
            out << (i + 1 < threadCounts.size() ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
        std::printf("\nJSON written to %s\n", jsonOutPath.c_str());
    }

    return 0;
}
