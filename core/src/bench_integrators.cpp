// 2d-2 항목7: 조화진동자(F(x)=-kx)에서 dt를 스윕하며 4개 적분기의 장기
// 에너지 드리프트를 비교한다. 순수 리포트 도구 -- 새 시뮬레이션 알고리즘
// 없음(AGENTS.md의 benchmark/analysis 도구 carve-out).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "physics/integrators.h"

namespace {

constexpr float kSpringConstant = 1.0f;  // k
constexpr float kMass = 1.0f;            // m
constexpr float kPeriod = 2.0f * static_cast<float>(M_PI);  // w = sqrt(k/m) = 1
constexpr float kPeriodsToRun = 20.0f;

float energyOf(const OscillatorState& s) {
    return 0.5f * kSpringConstant * s.position * s.position + 0.5f * kMass * s.velocity * s.velocity;
}

struct DriftResult {
    float initial;
    float maxEnergy;
    float minEnergy;
    float finalEnergy;
};

template <typename StepFn>
DriftResult sweepOne(StepFn step, float dt) {
    OscillatorState s{1.0f, 0.0f};
    DriftResult r{};
    r.initial = energyOf(s);
    r.maxEnergy = r.initial;
    r.minEnergy = r.initial;

    int steps = static_cast<int>(kPeriodsToRun * kPeriod / dt);
    for (int i = 0; i < steps; ++i) {
        step(s, kSpringConstant, kMass, dt);
        float e = energyOf(s);
        r.maxEnergy = std::max(r.maxEnergy, e);
        r.minEnergy = std::min(r.minEnergy, e);
    }
    r.finalEnergy = energyOf(s);
    return r;
}

struct Row {
    std::string integrator;
    int forceEvalsPerStep;
    float dt;
    float maxDriftPct;   // (maxEnergy/initial - 1) * 100
    float minDriftPct;   // (minEnergy/initial - 1) * 100
    float finalDriftPct; // (finalEnergy/initial - 1) * 100
};

void printAndCollect(std::vector<Row>& rows, const char* name, int forceEvals, float dt,
                      const DriftResult& r) {
    float maxDriftPct = (r.maxEnergy / r.initial - 1.0f) * 100.0f;
    float minDriftPct = (r.minEnergy / r.initial - 1.0f) * 100.0f;
    float finalDriftPct = (r.finalEnergy / r.initial - 1.0f) * 100.0f;
    std::printf("%-20s %-6d %-8.4f %12.2f%% %12.2f%% %12.2f%%\n", name, forceEvals, dt, maxDriftPct,
                minDriftPct, finalDriftPct);
    rows.push_back({name, forceEvals, dt, maxDriftPct, minDriftPct, finalDriftPct});
}

}  // namespace

int main(int argc, char** argv) {
    std::string jsonOutPath = (argc > 1) ? argv[1] : "";
    // 1.0f/60.0f: stepRigidBody()의 실제 운영 dt(training/env.py의 DT)와
    // 직접 비교하기 위해 스윕에 명시적으로 포함.
    const std::vector<float> dts = {0.001f, 1.0f / 60.0f, 0.05f, 0.1f, 0.2f};

    std::printf("integrator comparison on a harmonic oscillator (k=%.1f, m=%.1f, %d periods)\n",
                kSpringConstant, kMass, static_cast<int>(kPeriodsToRun));
    std::printf("%-20s %-6s %-8s %13s %13s %13s\n", "integrator", "evals", "dt", "max drift",
                "min drift", "final drift");

    std::vector<Row> rows;
    for (float dt : dts) {
        printAndCollect(rows, "explicit_euler", 1, dt, sweepOne(stepExplicitEuler, dt));
        printAndCollect(rows, "semi_implicit_euler", 1, dt, sweepOne(stepSemiImplicitEuler, dt));
        printAndCollect(rows, "velocity_verlet", 2, dt, sweepOne(stepVelocityVerlet, dt));
        printAndCollect(rows, "rk4", 4, dt, sweepOne(stepRK4, dt));
        std::printf("\n");
    }

    if (!jsonOutPath.empty()) {
        std::ofstream out(jsonOutPath);
        out << "{\n  \"rows\": [\n";
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            out << "    {\"integrator\": \"" << r.integrator << "\", \"force_evals_per_step\": "
                << r.forceEvalsPerStep << ", \"dt\": " << r.dt << ", \"max_drift_pct\": " << r.maxDriftPct
                << ", \"min_drift_pct\": " << r.minDriftPct << ", \"final_drift_pct\": " << r.finalDriftPct
                << "}";
            out << (i + 1 < rows.size() ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
        std::printf("JSON written to %s\n", jsonOutPath.c_str());
    }

    return 0;
}
