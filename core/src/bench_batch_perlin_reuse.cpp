// EXP-004 (benchmarks/experiments/perf-perlin-table-reuse.md): does
// allocating PerlinNoise's 512KB gradient table once per worker thread and
// reseeding in place (instead of allocating a fresh table on every reset)
// improve the throughput scaling measured in EXP-003
// (benchmarks/experiments/perf-parallel-envs.md), where 8-thread speedup
// (3.14x steps/sec, 2.95x resets/sec) fell short of the 4x acceptance bar?
//
// Same throughput-harness shape as core/src/bench_batch.cpp (persistent
// std::barrier-synchronized worker pool, not per-call thread spawn -- that
// mistake was already found and fixed in EXP-003). This file adds the one
// new variable: whether each worker's PerlinNoise is allocated fresh per
// reset (baseline) or allocated once and reseeded in place (candidate).
// Mechanical benchmark/orchestration code, not physics/erosion algorithm
// work -- PerlinNoise::reseed() itself was written by the project owner
// (core/src/noise/perlin_noise.{h,cpp}), not by this harness.
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

// Matches training/env.py's TerrainAgentEnv defaults, same as bench.cpp /
// bench_batch.cpp / training/bench_env.py.
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

enum class AllocMode { Baseline, Candidate };

// The candidate path needs a PerlinNoise that survives across resets, so
// it's a member of the slot (allocated once, when the slot itself is
// constructed) rather than a local in resetSlot. The baseline path ignores
// this member entirely and allocates its own local PerlinNoise every call,
// exactly like bench_batch.cpp's EXP-003 harness did.
struct EnvSlot {
    Heightmap terrain{kMapSize, kMapSize};
    RigidBody body{};
    PerlinNoise reusableNoise;  // only touched by the Candidate path
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

void fillTerrainAndBody(Heightmap& hm, EnvSlot& slot, const PerlinNoise& noise) {
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

// Baseline: allocates a brand-new PerlinNoise (fresh 512KB gx_/gy_ tables)
// every single reset -- current production behavior (env.py's reset() via
// ts.generate_fbm_heightmap).
void resetSlotBaseline(EnvSlot& slot, unsigned seed) {
    Heightmap hm(kMapSize, kMapSize);
    PerlinNoise noise;
    noise.reseed(seed);
    fillTerrainAndBody(hm, slot, noise);
}

// Candidate: table allocated once when the EnvSlot/pool was created; reset
// only calls reseed(), which refills the existing buffer -- no heap
// allocation on the reset path at all.
void resetSlotCandidate(EnvSlot& slot, unsigned seed) {
    Heightmap hm(kMapSize, kMapSize);
    slot.reusableNoise.reseed(seed);
    fillTerrainAndBody(hm, slot, slot.reusableNoise);
}

void stepSlot(EnvSlot& slot, int numSteps) {
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

EnvResult resultOf(const EnvSlot& slot) {
    return EnvResult{slot.body.position.x, slot.body.position.y, slot.body.position.z,
                      slot.body.velocity.x, slot.body.velocity.y, slot.body.velocity.z,
                      checksum(slot.terrain)};
}

// --- Correctness gate: candidate must match baseline bit-for-bit for the
// same seeds (perf-perlin-table-reuse.md's precondition). Sequential, no
// threading involved -- this only tests that reseed()-in-place produces the
// same table as a fresh PerlinNoise, independent of the parallelism story. ---
bool candidateMatchesBaseline(int numEnvs) {
    bool allMatch = true;
    for (int i = 0; i < numEnvs; ++i) {
        EnvSlot baselineSlot;
        resetSlotBaseline(baselineSlot, static_cast<unsigned>(i));
        EnvResult baselineResult = resultOf(baselineSlot);

        EnvSlot candidateSlot;
        resetSlotCandidate(candidateSlot, static_cast<unsigned>(i));
        EnvResult candidateResult = resultOf(candidateSlot);

        if (!(baselineResult == candidateResult)) {
            allMatch = false;
            std::printf("MISMATCH at seed %d: baseline vs candidate terrain/body differ\n", i);
        }
    }
    return allMatch;
}

// --- Parallel == sequential gate, repeated per mode (mirrors EXP-003's
// existing gate, now checked for both baseline and candidate allocation). ---
std::vector<EnvResult> runSequential(AllocMode mode, int numEnvs, int stepsPerEnv) {
    std::vector<EnvResult> results(numEnvs);
    for (int i = 0; i < numEnvs; ++i) {
        EnvSlot slot;
        if (mode == AllocMode::Baseline) {
            resetSlotBaseline(slot, static_cast<unsigned>(i));
        } else {
            resetSlotCandidate(slot, static_cast<unsigned>(i));
        }
        stepSlot(slot, stepsPerEnv);
        results[i] = resultOf(slot);
    }
    return results;
}

// Persistent worker pool (same std::barrier design as EXP-003's
// bench_batch.cpp -- thread creation cost already ruled out there, not
// re-litigated here). Reset dispatches to the baseline or candidate function
// depending on `mode`.
enum class Phase { Reset, Step, Stop };

class WorkerPool {
public:
    WorkerPool(int threadCount, int stepsPerEnv, AllocMode mode)
        : slots_(threadCount),
          stepsPerEnv_(stepsPerEnv),
          mode_(mode),
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
                if (mode_ == AllocMode::Baseline) {
                    resetSlotBaseline(slots_[i], static_cast<unsigned>(i));
                } else {
                    resetSlotCandidate(slots_[i], static_cast<unsigned>(i));
                }
            } else {
                stepSlot(slots_[i], stepsPerEnv_);
            }
            endBarrier_.arrive_and_wait();
        }
    }

    std::vector<EnvSlot> slots_;
    int stepsPerEnv_;
    AllocMode mode_;
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

std::vector<SweepPoint> runSweep(AllocMode mode, const std::vector<int>& threadCounts, int repeats,
                                  int episodesPerStepPhase) {
    std::vector<SweepPoint> sweep;
    for (int tc : threadCounts) {
        WorkerPool pool(tc, episodesPerStepPhase * kStepsPerEpisode, mode);
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

    // --- Correctness gate 1: candidate reseed()-in-place produces the same
    // terrain as a fresh baseline PerlinNoise, for the same seeds. ---
    constexpr int kCorrectnessEnvs = 8;
    bool candidateOk = candidateMatchesBaseline(kCorrectnessEnvs);
    std::printf("Correctness check 1 (candidate reseed == baseline fresh-alloc, %d seeds): %s\n",
                kCorrectnessEnvs, candidateOk ? "PASS" : "FAIL");

    // --- Correctness gate 2: parallel == sequential, for each mode. ---
    constexpr int kCorrectnessSteps = 200;
    bool parallelOk = true;
    for (AllocMode mode : {AllocMode::Baseline, AllocMode::Candidate}) {
        auto seq = runSequential(mode, kCorrectnessEnvs, kCorrectnessSteps);
        WorkerPool pool(kCorrectnessEnvs, kCorrectnessSteps, mode);
        pool.runPhase(Phase::Reset);
        pool.runPhase(Phase::Step);
        auto par = pool.collectResults();
        for (int i = 0; i < kCorrectnessEnvs; ++i) {
            if (!(seq[i] == par[i])) {
                parallelOk = false;
                std::printf("MISMATCH (mode=%s) at env %d: sequential vs parallel differ\n",
                            mode == AllocMode::Baseline ? "baseline" : "candidate", i);
            }
        }
    }
    std::printf("Correctness check 2 (parallel == sequential, both modes): %s\n\n",
                parallelOk ? "PASS" : "FAIL");

    if (!candidateOk || !parallelOk) {
        std::printf("Aborting throughput sweep -- correctness gate failed "
                    "(perf-perlin-table-reuse.md: Rejected regardless of speed).\n");
        return 1;
    }

    // --- Throughput sweep, baseline and candidate, same grid as EXP-003. ---
    const std::vector<int> threadCounts = {1, 2, 4, 6, 8, 12, 16};
    constexpr int kRepeats = 7;
    constexpr int kEpisodesPerStepPhase = 50;

    auto baselineSweep = runSweep(AllocMode::Baseline, threadCounts, kRepeats, kEpisodesPerStepPhase);
    auto candidateSweep = runSweep(AllocMode::Candidate, threadCounts, kRepeats, kEpisodesPerStepPhase);

    std::printf("%-10s %-26s %-26s %-24s %-24s\n", "threads", "resets/s baseline(mean+-sd)",
                "resets/s candidate(mean+-sd)", "steps/s baseline(mean+-sd)",
                "steps/s candidate(mean+-sd)");
    for (size_t i = 0; i < threadCounts.size(); ++i) {
        const auto& b = baselineSweep[i];
        const auto& c = candidateSweep[i];
        std::printf("%-10d %8.1f +- %-8.1f %8.1f +- %-8.1f %10.1f +- %-8.1f %10.1f +- %-8.1f\n",
                    threadCounts[i], b.resetsPerSecMean, b.resetsPerSecSd, c.resetsPerSecMean,
                    c.resetsPerSecSd, b.stepsPerSecMean, b.stepsPerSecSd, c.stepsPerSecMean,
                    c.stepsPerSecSd);
    }

    // Improvement at thread_count == physical core count (8), per
    // perf-perlin-table-reuse.md's pre-registered acceptance criteria.
    auto it8 = std::find(threadCounts.begin(), threadCounts.end(), 8);
    if (it8 != threadCounts.end()) {
        size_t idx8 = static_cast<size_t>(it8 - threadCounts.begin());
        double resetsImprovementPct =
            100.0 * (candidateSweep[idx8].resetsPerSecMean / baselineSweep[idx8].resetsPerSecMean - 1.0);
        double stepsImprovementPct =
            100.0 * (candidateSweep[idx8].stepsPerSecMean / baselineSweep[idx8].stepsPerSecMean - 1.0);
        std::printf("\nAt thread_count=8: resets/sec improvement = %.1f%%, steps/sec improvement = %.1f%%\n",
                    resetsImprovementPct, stepsImprovementPct);
        const char* verdict = "Inconclusive";
        if (resetsImprovementPct >= 50.0 && stepsImprovementPct >= 50.0) verdict = "Accepted";
        if (resetsImprovementPct < 15.0 && stepsImprovementPct < 15.0) verdict = "Rejected";
        std::printf("Verdict against perf-perlin-table-reuse.md's pre-registered criteria: %s\n",
                    verdict);
    }

    if (!jsonOutPath.empty()) {
        std::ofstream out(jsonOutPath);
        out << "{\n  \"correctness_check_pass\": true,\n  \"sweep\": [\n";
        for (size_t i = 0; i < threadCounts.size(); ++i) {
            const auto& b = baselineSweep[i];
            const auto& c = candidateSweep[i];
            out << "    {\"thread_count\": " << threadCounts[i]
                << ", \"resets_per_sec_baseline_mean\": " << b.resetsPerSecMean
                << ", \"resets_per_sec_baseline_sd\": " << b.resetsPerSecSd
                << ", \"resets_per_sec_candidate_mean\": " << c.resetsPerSecMean
                << ", \"resets_per_sec_candidate_sd\": " << c.resetsPerSecSd
                << ", \"steps_per_sec_baseline_mean\": " << b.stepsPerSecMean
                << ", \"steps_per_sec_baseline_sd\": " << b.stepsPerSecSd
                << ", \"steps_per_sec_candidate_mean\": " << c.stepsPerSecMean
                << ", \"steps_per_sec_candidate_sd\": " << c.stepsPerSecSd << "}";
            out << (i + 1 < threadCounts.size() ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
        std::printf("\nJSON written to %s\n", jsonOutPath.c_str());
    }

    return 0;
}
