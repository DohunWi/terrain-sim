// EXP-006 (benchmarks/experiments/perf-perlin-reuse-aligned.md): re-tests
// EXP-004's PerlinNoise alloc-once-and-reseed idea
// (benchmarks/experiments/perf-perlin-table-reuse.md), but this time baseline
// and candidate use the *same* EnvSlot type (alignas(64), with a resident
// PerlinNoise member present in both) so struct layout can't confound the
// allocation-policy comparison the way it did in EXP-004.
//
// Same std::barrier persistent-worker-pool harness as the earlier bench_batch*
// files. Mechanical benchmark/orchestration code, not physics/erosion
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

enum class AllocMode { Baseline, Candidate };

// One type, used by both modes -- baseline mode ignores `noise` and creates
// its own local PerlinNoise per reset; candidate mode reseeds `noise` in
// place. Since it's the same type either way, sizeof/alignof is identical
// regardless of which mode is running, which is the whole point: EXP-004's
// baseline and candidate used two *different* types (candidate had an extra
// member), which confounded the allocation-policy comparison with a
// layout/false-sharing difference. alignas(64) keeps EXP-005's reset-phase
// improvement in both modes here.
struct alignas(64) EnvSlot {
    Heightmap terrain{kMapSize, kMapSize};
    RigidBody body{};
    PerlinNoise noise;
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

void resetSlotBaseline(EnvSlot& slot, unsigned seed) {
    Heightmap hm(kMapSize, kMapSize);
    PerlinNoise localNoise;
    localNoise.reseed(seed);
    fillTerrainAndBody(hm, slot, localNoise);
}

void resetSlotCandidate(EnvSlot& slot, unsigned seed) {
    Heightmap hm(kMapSize, kMapSize);
    slot.noise.reseed(seed);
    fillTerrainAndBody(hm, slot, slot.noise);
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

    std::printf("sizeof(EnvSlot) = %zu, alignof = %zu (same type for both modes)\n\n",
                sizeof(EnvSlot), alignof(EnvSlot));

    constexpr int kCorrectnessEnvs = 8;
    bool candidateOk = candidateMatchesBaseline(kCorrectnessEnvs);
    std::printf("Correctness check 1 (candidate reseed == baseline fresh-alloc, %d seeds): %s\n",
                kCorrectnessEnvs, candidateOk ? "PASS" : "FAIL");

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
                    "(perf-perlin-reuse-aligned.md: Rejected regardless of speed).\n");
        return 1;
    }

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
        if (resetsImprovementPct >= 20.0 && stepsImprovementPct >= 20.0) verdict = "Accepted";
        if (std::abs(resetsImprovementPct) <= 10.0 && std::abs(stepsImprovementPct) <= 10.0)
            verdict = "Rejected";
        if (resetsImprovementPct < -10.0 && stepsImprovementPct < -10.0) verdict = "Rejected";
        std::printf("Verdict against perf-perlin-reuse-aligned.md's pre-registered criteria: %s\n",
                    verdict);
    }

    if (!jsonOutPath.empty()) {
        std::ofstream out(jsonOutPath);
        out << "{\n  \"correctness_check_pass\": true,\n"
            << "  \"sizeof_envslot\": " << sizeof(EnvSlot) << ",\n"
            << "  \"sweep\": [\n";
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
