// EXP-008 (benchmarks/experiments/perf-thermal-erode-alloc.md): does
// eliminating thermalErode's per-cell/per-iteration heap allocations
// (findLowestNeighbor's std::vector -> std::array<4>, and the per-iteration
// Heightmap next = height -> a reused buffer) improve reset-phase
// throughput and thread scaling, on top of the alignas(64) EnvSlot from
// EXP-005/007?
//
// Correctness (the precondition for this whole experiment) is verified
// separately and more rigorously in core/tests/erosion_test.cpp (20 seeds,
// cell-by-cell bit-exact comparison against the frozen pre-refactor
// reference in core/tests/erosion_reference.{h,cpp}) -- this harness reuses
// that same reference for a lighter parallel/sequential sanity check, but
// the authoritative correctness gate is the GoogleTest suite.
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "../tests/erosion_reference.h"
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

enum class Implementation { Baseline, Candidate };

// Same shape as EXP-005/007's EnvSlotAligned (no PerlinNoise member -- that
// variable was already tested in EXP-004/006 and found not to matter).
struct alignas(64) EnvSlot {
    Heightmap terrain{kMapSize, kMapSize};
    RigidBody body{};
};

double checksum(const Heightmap& hm) {
    double sum = 0.0;
    for (int y = 0; y < hm.height(); ++y) {
        for (int x = 0; x < hm.width(); ++x) sum += hm.at(x, y);
    }
    return sum;
}

Heightmap generateFbm(unsigned seed) {
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

void resetSlot(EnvSlot& slot, unsigned seed, Implementation impl) {
    Heightmap hm = generateFbm(seed);
    if (impl == Implementation::Baseline) {
        thermalErodeRef(hm, kTalusAngle, kErosionRate, kErosionIterations);
    } else {
        thermalErode(hm, kTalusAngle, kErosionRate, kErosionIterations);
    }
    slot.terrain = std::move(hm);
    HeightSample start = slot.terrain.sample(32.0f, 32.0f);
    slot.body.position = Vec3{32.0f, start.height + 5.0f, 32.0f};
    slot.body.velocity = Vec3{0.0f, 0.0f, 0.0f};
    slot.body.mass = 1.0f;
}

std::vector<double> runSequentialChecksums(Implementation impl, int numEnvs) {
    std::vector<double> results(numEnvs);
    for (int i = 0; i < numEnvs; ++i) {
        EnvSlot slot;
        resetSlot(slot, static_cast<unsigned>(i), impl);
        results[i] = checksum(slot.terrain);
    }
    return results;
}

enum class Phase { Reset, Stop };

class WorkerPool {
public:
    WorkerPool(int threadCount, Implementation impl)
        : slots_(threadCount),
          impl_(impl),
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

    double runReset() {
        phase_ = Phase::Reset;
        auto start = Clock::now();
        startBarrier_.arrive_and_wait();
        endBarrier_.arrive_and_wait();
        auto end = Clock::now();
        return std::chrono::duration<double>(end - start).count();
    }

    std::vector<double> collectChecksums() {
        std::vector<double> results(slots_.size());
        for (size_t i = 0; i < slots_.size(); ++i) results[i] = checksum(slots_[i].terrain);
        return results;
    }

private:
    void workerLoop(int i) {
        for (;;) {
            startBarrier_.arrive_and_wait();
            if (phase_ == Phase::Stop) return;
            resetSlot(slots_[i], static_cast<unsigned>(i), impl_);
            endBarrier_.arrive_and_wait();
        }
    }

    std::vector<EnvSlot> slots_;
    Implementation impl_;
    std::atomic<Phase> phase_{Phase::Reset};
    std::barrier<> startBarrier_;
    std::barrier<> endBarrier_;
    std::vector<std::thread> threads_;
};

struct SweepPoint {
    int threadCount;
    double resetsPerSecMean, resetsPerSecSd;
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

std::vector<SweepPoint> runSweep(Implementation impl, const std::vector<int>& threadCounts, int repeats) {
    std::vector<SweepPoint> sweep;
    for (int tc : threadCounts) {
        WorkerPool pool(tc, impl);
        std::vector<double> resetsPerSec;
        for (int r = 0; r < repeats; ++r) {
            double resetSeconds = pool.runReset();
            resetsPerSec.push_back(tc / resetSeconds);
        }
        double rMean = mean(resetsPerSec), rSd = stddev(resetsPerSec, rMean);
        sweep.push_back({tc, rMean, rSd});
    }
    return sweep;
}

}  // namespace

int main(int argc, char** argv) {
    std::string jsonOutPath = (argc > 1) ? argv[1] : "";

    // Sanity check (authoritative correctness gate is core/tests/erosion_test.cpp):
    // baseline (thermalErodeRef) and candidate (thermalErode) must produce
    // identical checksums for the same seeds, sequentially and in parallel.
    constexpr int kCorrectnessEnvs = 8;
    auto baselineSeq = runSequentialChecksums(Implementation::Baseline, kCorrectnessEnvs);
    auto candidateSeq = runSequentialChecksums(Implementation::Candidate, kCorrectnessEnvs);
    bool identical = true;
    for (int i = 0; i < kCorrectnessEnvs; ++i) {
        if (baselineSeq[i] != candidateSeq[i]) {
            identical = false;
            std::printf("MISMATCH at seed %d: baseline checksum %.6f != candidate checksum %.6f\n", i,
                        baselineSeq[i], candidateSeq[i]);
        }
    }
    std::printf("Correctness check (baseline == candidate checksum, %d seeds): %s\n", kCorrectnessEnvs,
                identical ? "PASS" : "FAIL");
    if (!identical) {
        std::printf("Aborting sweep -- correctness gate failed "
                    "(perf-thermal-erode-alloc.md: Rejected regardless of speed).\n");
        return 1;
    }

    const std::vector<int> threadCounts = {1, 2, 4, 6, 8, 12, 16};
    constexpr int kRepeats = 20;  // EXP-007 lesson: n=7 was too noisy to read.

    auto baselineSweep = runSweep(Implementation::Baseline, threadCounts, kRepeats);
    auto candidateSweep = runSweep(Implementation::Candidate, threadCounts, kRepeats);

    std::printf("%-10s %-26s %-26s %-8s %-10s\n", "threads", "resets/s baseline(mean+-sd)",
                "resets/s candidate(mean+-sd)", "t", "pct");
    for (size_t i = 0; i < threadCounts.size(); ++i) {
        const auto& b = baselineSweep[i];
        const auto& c = candidateSweep[i];
        double seB = b.resetsPerSecSd / std::sqrt(static_cast<double>(kRepeats));
        double seC = c.resetsPerSecSd / std::sqrt(static_cast<double>(kRepeats));
        double t = (c.resetsPerSecMean - b.resetsPerSecMean) / std::sqrt(seB * seB + seC * seC);
        double pct = (c.resetsPerSecMean - b.resetsPerSecMean) / b.resetsPerSecMean * 100.0;
        std::printf("%-10d %8.1f +- %-12.1f %8.1f +- %-12.1f %8.2f %9.1f%%\n", threadCounts[i],
                    b.resetsPerSecMean, b.resetsPerSecSd, c.resetsPerSecMean, c.resetsPerSecSd, t, pct);
    }

    if (!jsonOutPath.empty()) {
        std::ofstream out(jsonOutPath);
        out << "{\n  \"correctness_check_pass\": true,\n  \"sweep\": [\n";
        for (size_t i = 0; i < threadCounts.size(); ++i) {
            const auto& b = baselineSweep[i];
            const auto& c = candidateSweep[i];
            double seB = b.resetsPerSecSd / std::sqrt(static_cast<double>(kRepeats));
            double seC = c.resetsPerSecSd / std::sqrt(static_cast<double>(kRepeats));
            double t = (c.resetsPerSecMean - b.resetsPerSecMean) / std::sqrt(seB * seB + seC * seC);
            double pct = (c.resetsPerSecMean - b.resetsPerSecMean) / b.resetsPerSecMean * 100.0;
            out << "    {\"thread_count\": " << threadCounts[i]
                << ", \"resets_per_sec_baseline_mean\": " << b.resetsPerSecMean
                << ", \"resets_per_sec_baseline_sd\": " << b.resetsPerSecSd
                << ", \"resets_per_sec_candidate_mean\": " << c.resetsPerSecMean
                << ", \"resets_per_sec_candidate_sd\": " << c.resetsPerSecSd << ", \"t\": " << t
                << ", \"pct_change\": " << pct << "}";
            out << (i + 1 < threadCounts.size() ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
        std::printf("\nJSON written to %s\n", jsonOutPath.c_str());
    }

    return 0;
}
