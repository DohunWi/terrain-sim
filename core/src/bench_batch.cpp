// EXP-003 (benchmarks/experiments/perf-parallel-envs.md): does spreading N
// independent env reset/step workloads across std::thread workers actually
// scale aggregate throughput, and does parallel execution stay bit-identical
// to sequential single-thread execution for the same seeds?
//
// This is a throughput/correctness measurement harness over existing core/
// primitives (Heightmap/PerlinNoise/thermalErode/RigidBody) -- no new
// simulation algorithm, no shared mutable state between env slots, so it's
// mechanical/orchestration code (AGENTS.md's benchmark-tool carve-out), not
// physics/collision/erosion algorithm work.
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

// Matches training/env.py's TerrainAgentEnv defaults (same workload as
// core/src/bench.cpp and training/bench_env.py, so all three are comparable).
constexpr int kMapSize = 64;
constexpr float kScale = 10.0f;
constexpr int kOctaves = 3;
constexpr float kPersistence = 0.5f;
constexpr float kLacunarity = 2.0f;
constexpr float kTalusAngle = 0.15f;
constexpr float kErosionRate = 0.3f;
constexpr int kErosionIterations = 10;
constexpr float kDt = 1.0f / 60.0f;
constexpr int kStepsPerEpisode = 1000;  // matches MAX_STEPS

// One env's full state, independent of every other env -- no shared mutable
// data between threads, so no synchronization is needed inside a worker.
struct EnvSlot {
    Heightmap terrain{kMapSize, kMapSize};
    RigidBody body{};
};

// Bit-comparable summary of one env's final state, used to verify parallel
// and sequential execution produced identical results (not "close" -- exactly
// equal, since it's the same deterministic arithmetic run in a different
// thread, not a different algorithm).
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

void resetSlot(EnvSlot& slot, unsigned seed) {
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

// Runs `numEnvs` reset+step workloads sequentially on the calling thread.
std::vector<EnvResult> runSequential(int numEnvs, int stepsPerEnv) {
    std::vector<EnvResult> results(numEnvs);
    for (int i = 0; i < numEnvs; ++i) {
        EnvSlot slot;
        resetSlot(slot, static_cast<unsigned>(i));
        stepSlot(slot, stepsPerEnv);
        results[i] = resultOf(slot);
    }
    return results;
}

// Runs `numEnvs` reset+step workloads across `threadCount` worker threads,
// one env per thread (threadCount == numEnvs, per the experiment's declared
// independent variable). Returns the batch wall-clock time for reset and for
// step separately, plus each env's final result (for the correctness check).
struct BatchTiming {
    double resetSeconds;
    double stepSeconds;
    std::vector<EnvResult> results;
};

BatchTiming runParallelBatch(int threadCount, int stepsPerEnv) {
    std::vector<EnvSlot> slots(threadCount);
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    auto resetStart = Clock::now();
    for (int i = 0; i < threadCount; ++i) {
        workers.emplace_back([&slots, i] { resetSlot(slots[i], static_cast<unsigned>(i)); });
    }
    for (auto& t : workers) t.join();
    auto resetEnd = Clock::now();
    workers.clear();

    auto stepStart = Clock::now();
    for (int i = 0; i < threadCount; ++i) {
        workers.emplace_back([&slots, i, stepsPerEnv] { stepSlot(slots[i], stepsPerEnv); });
    }
    for (auto& t : workers) t.join();
    auto stepEnd = Clock::now();

    std::vector<EnvResult> results(threadCount);
    for (int i = 0; i < threadCount; ++i) results[i] = resultOf(slots[i]);

    return BatchTiming{std::chrono::duration<double>(resetEnd - resetStart).count(),
                        std::chrono::duration<double>(stepEnd - stepStart).count(), results};
}

// Persistent worker pool for the throughput sweep. Spawning kThreadCount
// std::threads fresh for every one of the ~1000-step batches (as
// runParallelBatch above does) turned out to make the "steps/sec" numbers
// meaningless: a single stepRigidBody call is ~0.02us, so 1000 of them per
// env finish in ~20us -- smaller than std::thread creation/join cost, so the
// very first sweep run measured thread-spawn overhead, not step throughput.
// This pool creates `threadCount` threads once and reuses them across every
// repeat via std::barrier, matching how a real VecEnv would actually be used
// (workers created once at env-init, not per rollout step).
enum class Phase { Reset, Step, Stop };

class WorkerPool {
public:
    explicit WorkerPool(int threadCount, int stepsPerEnv)
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

    // Runs one phase (reset or step) across all workers, waits for
    // completion, and returns the wall-clock duration of just that phase
    // (thread wake/dispatch latency included, thread creation excluded --
    // that's the whole point of reusing the pool across repeats).
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

    std::vector<EnvSlot> slots_;
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

}  // namespace

int main(int argc, char** argv) {
    std::string jsonOutPath = (argc > 1) ? argv[1] : "";

    // --- Correctness gate (pre-registered in perf-parallel-envs.md): parallel
    // execution must be bit-identical to sequential for the same seeds. ---
    constexpr int kCorrectnessEnvs = 8;
    constexpr int kCorrectnessSteps = 200;
    auto sequentialResults = runSequential(kCorrectnessEnvs, kCorrectnessSteps);
    auto parallelBatch = runParallelBatch(kCorrectnessEnvs, kCorrectnessSteps);
    bool identical = true;
    for (int i = 0; i < kCorrectnessEnvs; ++i) {
        if (!(sequentialResults[i] == parallelBatch.results[i])) {
            identical = false;
            std::printf("MISMATCH at env %d: sequential vs parallel results differ\n", i);
        }
    }
    std::printf("Correctness check (parallel == sequential, %d envs, %d steps each): %s\n\n",
                kCorrectnessEnvs, kCorrectnessSteps, identical ? "PASS" : "FAIL");
    if (!identical) {
        std::printf("Aborting throughput sweep -- correctness gate failed "
                    "(perf-parallel-envs.md: Rejected regardless of speed).\n");
        return 1;
    }

    // --- Throughput sweep ---
    const std::vector<int> threadCounts = {1, 2, 4, 6, 8, 12, 16};
    constexpr int kRepeats = 7;
    // A single stepRigidBody call is ~0.02us, so kStepsPerEpisode (1000) steps
    // finishes in ~20us per env -- too small to amortize even pooled-thread
    // wake latency. Run many episodes' worth of steps per measured "step"
    // phase so the real compute dominates dispatch overhead; resets/sec still
    // uses one reset per phase since a single reset (~5ms) already dwarfs
    // dispatch cost by three orders of magnitude.
    constexpr int kEpisodesPerStepPhase = 50;
    constexpr int kStepsPerPhase = kEpisodesPerStepPhase * kStepsPerEpisode;

    std::printf("%-12s %-24s %-24s %-10s\n", "threads", "resets/sec (mean+-sd)",
                "steps/sec (mean+-sd)", "speedup");
    std::vector<SweepPoint> sweep;
    double baselineStepsPerSec = 0.0;

    for (int tc : threadCounts) {
        WorkerPool pool(tc, kStepsPerPhase);
        std::vector<double> resetsPerSec, stepsPerSec;
        for (int r = 0; r < kRepeats; ++r) {
            double resetSeconds = pool.runPhase(Phase::Reset);
            resetsPerSec.push_back(tc / resetSeconds);
            double stepSeconds = pool.runPhase(Phase::Step);
            stepsPerSec.push_back((static_cast<double>(tc) * kStepsPerPhase) / stepSeconds);
        }
        double rMean = mean(resetsPerSec), rSd = stddev(resetsPerSec, rMean);
        double sMean = mean(stepsPerSec), sSd = stddev(stepsPerSec, sMean);
        if (tc == 1) baselineStepsPerSec = sMean;
        double speedup = sMean / baselineStepsPerSec;

        std::printf("%-12d %10.1f +- %-10.1f %10.1f +- %-10.1f %9.2fx\n", tc, rMean, rSd, sMean, sSd,
                    speedup);
        sweep.push_back({tc, rMean, rSd, sMean, sSd});
    }

    if (!jsonOutPath.empty()) {
        std::ofstream out(jsonOutPath);
        out << "{\n  \"correctness_check_pass\": true,\n  \"sweep\": [\n";
        for (size_t i = 0; i < sweep.size(); ++i) {
            const auto& p = sweep[i];
            out << "    {\"thread_count\": " << p.threadCount << ", \"resets_per_sec_mean\": "
                << p.resetsPerSecMean << ", \"resets_per_sec_sd\": " << p.resetsPerSecSd
                << ", \"steps_per_sec_mean\": " << p.stepsPerSecMean << ", \"steps_per_sec_sd\": "
                << p.stepsPerSecSd << ", \"speedup_vs_thread_count_1\": "
                << (p.stepsPerSecMean / baselineStepsPerSec) << "}";
            out << (i + 1 < sweep.size() ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
        std::printf("\nJSON written to %s\n", jsonOutPath.c_str());
    }

    return 0;
}
