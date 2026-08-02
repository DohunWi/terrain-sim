// EXP-007 (benchmarks/experiments/perf-thermal-erode-bandwidth.md): does
// thermalErode's repeated whole-grid traversal (10 iterations, each reading
// every cell's 4 neighbors and writing a new buffer) cap reset-phase
// scaling independently of the false-sharing issue EXP-005 already fixed?
// Compares scaling-efficiency curves (speedup(N) = resets/sec(N)/resets/sec(1))
// for two reset workloads: fbm+thermalErode (production reset() workload)
// vs fbm-only (thermalErode removed) -- not their absolute throughput, which
// isn't a fair comparison since the workloads do different amounts of work.
//
// Same std::barrier persistent-worker-pool harness as the earlier bench_batch*
// files. Mechanical benchmark/orchestration code, not physics/erosion
// algorithm work.
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

enum class Workload { FbmPlusThermal, FbmOnly };

// Matches EXP-005's EnvSlotAligned (no PerlinNoise member -- that variable
// was already tested and found to not matter in EXP-004/006, out of scope
// here).
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

void resetSlot(EnvSlot& slot, unsigned seed, Workload workload) {
    Heightmap hm(kMapSize, kMapSize);
    PerlinNoise noise;
    noise.reseed(seed);
    for (int y = 0; y < kMapSize; ++y) {
        for (int x = 0; x < kMapSize; ++x) {
            hm.at(x, y) = noise.fbm(static_cast<float>(x) / kScale, static_cast<float>(y) / kScale,
                                     kOctaves, kPersistence, kLacunarity);
        }
    }
    if (workload == Workload::FbmPlusThermal) {
        thermalErode(hm, kTalusAngle, kErosionRate, kErosionIterations);
    }
    slot.terrain = std::move(hm);
    HeightSample start = slot.terrain.sample(32.0f, 32.0f);
    slot.body.position = Vec3{32.0f, start.height + 5.0f, 32.0f};
    slot.body.velocity = Vec3{0.0f, 0.0f, 0.0f};
    slot.body.mass = 1.0f;
}

// Correctness gate: parallel == sequential for a given workload (checksum
// only -- there's no baseline/candidate equivalence to check here, the two
// workloads are deliberately different).
std::vector<double> runSequentialChecksums(Workload workload, int numEnvs) {
    std::vector<double> results(numEnvs);
    for (int i = 0; i < numEnvs; ++i) {
        EnvSlot slot;
        resetSlot(slot, static_cast<unsigned>(i), workload);
        results[i] = checksum(slot.terrain);
    }
    return results;
}

enum class Phase { Reset, Stop };

class WorkerPool {
public:
    WorkerPool(int threadCount, Workload workload)
        : slots_(threadCount),
          workload_(workload),
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
            resetSlot(slots_[i], static_cast<unsigned>(i), workload_);
            endBarrier_.arrive_and_wait();
        }
    }

    std::vector<EnvSlot> slots_;
    Workload workload_;
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

std::vector<SweepPoint> runSweep(Workload workload, const std::vector<int>& threadCounts, int repeats) {
    std::vector<SweepPoint> sweep;
    for (int tc : threadCounts) {
        WorkerPool pool(tc, workload);
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

    // --- Correctness gate: parallel == sequential, for each workload. ---
    constexpr int kCorrectnessEnvs = 8;
    bool identical = true;
    for (Workload w : {Workload::FbmPlusThermal, Workload::FbmOnly}) {
        auto seq = runSequentialChecksums(w, kCorrectnessEnvs);
        WorkerPool pool(kCorrectnessEnvs, w);
        pool.runReset();
        auto par = pool.collectChecksums();
        for (int i = 0; i < kCorrectnessEnvs; ++i) {
            if (seq[i] != par[i]) {
                identical = false;
                std::printf("MISMATCH (workload=%s) at env %d: sequential vs parallel checksum differ\n",
                            w == Workload::FbmPlusThermal ? "fbm+thermal" : "fbm-only", i);
            }
        }
    }
    std::printf("Correctness check (parallel == sequential, both workloads): %s\n\n",
                identical ? "PASS" : "FAIL");
    if (!identical) {
        std::printf("Aborting sweep -- correctness gate failed.\n");
        return 1;
    }

    const std::vector<int> threadCounts = {1, 2, 4, 6, 8, 12, 16};
    // Bumped 7 -> 20 (perf-thermal-erode-bandwidth.md's first pass at n=7 was
    // too noisy to tell whether the 12/16-thread divergence was real or
    // noise -- more repeats tightens SE_mean1/SE_meanN enough for the delta-
    // method ratio test below to mean something).
    constexpr int kRepeats = 20;

    auto baselineSweep = runSweep(Workload::FbmPlusThermal, threadCounts, kRepeats);
    auto candidateSweep = runSweep(Workload::FbmOnly, threadCounts, kRepeats);

    double baseline1Thread = baselineSweep[0].resetsPerSecMean;
    double candidate1Thread = candidateSweep[0].resetsPerSecMean;
    double baseline1ThreadSe = baselineSweep[0].resetsPerSecSd / std::sqrt(static_cast<double>(kRepeats));
    double candidate1ThreadSe =
        candidateSweep[0].resetsPerSecSd / std::sqrt(static_cast<double>(kRepeats));

    // docs/evaluation-protocol.md §18's ratio-comparison extension: SE of a
    // ratio (speedup = mean_N/mean_1) via first-order delta method, then a
    // two-sample t on the two (independent) speedup estimates.
    auto speedupAndSe = [&](double meanN, double sdN, double mean1, double se1) {
        double seN = sdN / std::sqrt(static_cast<double>(kRepeats));
        double speedup = meanN / mean1;
        double relVar = (seN / meanN) * (seN / meanN) + (se1 / mean1) * (se1 / mean1);
        double seSpeedup = speedup * std::sqrt(relVar);
        return std::pair<double, double>{speedup, seSpeedup};
    };

    std::printf("%-10s %-24s %-24s %-12s %-12s %-8s\n", "threads", "resets/s baseline(mean+-sd)",
                "resets/s candidate(mean+-sd)", "speedup(b)", "speedup(c)", "t(ratio)");
    for (size_t i = 0; i < threadCounts.size(); ++i) {
        const auto& b = baselineSweep[i];
        const auto& c = candidateSweep[i];
        auto [speedupB, seSpeedupB] = speedupAndSe(b.resetsPerSecMean, b.resetsPerSecSd, baseline1Thread,
                                                    baseline1ThreadSe);
        auto [speedupC, seSpeedupC] = speedupAndSe(c.resetsPerSecMean, c.resetsPerSecSd, candidate1Thread,
                                                    candidate1ThreadSe);
        double tRatio = (speedupC - speedupB) / std::sqrt(seSpeedupB * seSpeedupB + seSpeedupC * seSpeedupC);
        std::printf("%-10d %8.1f +- %-12.1f %8.1f +- %-12.1f %9.2fx %9.2fx %8.2f\n", threadCounts[i],
                    b.resetsPerSecMean, b.resetsPerSecSd, c.resetsPerSecMean, c.resetsPerSecSd, speedupB,
                    speedupC, tRatio);
    }

    if (!jsonOutPath.empty()) {
        std::ofstream out(jsonOutPath);
        out << "{\n  \"correctness_check_pass\": true,\n  \"sweep\": [\n";
        for (size_t i = 0; i < threadCounts.size(); ++i) {
            const auto& b = baselineSweep[i];
            const auto& c = candidateSweep[i];
            auto [speedupB, seSpeedupB] = speedupAndSe(b.resetsPerSecMean, b.resetsPerSecSd,
                                                        baseline1Thread, baseline1ThreadSe);
            auto [speedupC, seSpeedupC] = speedupAndSe(c.resetsPerSecMean, c.resetsPerSecSd,
                                                        candidate1Thread, candidate1ThreadSe);
            double tRatio =
                (speedupC - speedupB) / std::sqrt(seSpeedupB * seSpeedupB + seSpeedupC * seSpeedupC);
            out << "    {\"thread_count\": " << threadCounts[i]
                << ", \"resets_per_sec_baseline_mean\": " << b.resetsPerSecMean
                << ", \"resets_per_sec_baseline_sd\": " << b.resetsPerSecSd
                << ", \"resets_per_sec_candidate_mean\": " << c.resetsPerSecMean
                << ", \"resets_per_sec_candidate_sd\": " << c.resetsPerSecSd
                << ", \"speedup_baseline\": " << speedupB << ", \"speedup_candidate\": " << speedupC
                << ", \"t_ratio\": " << tRatio << "}";
            out << (i + 1 < threadCounts.size() ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
        std::printf("\nJSON written to %s\n", jsonOutPath.c_str());
    }

    return 0;
}
