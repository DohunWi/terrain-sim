// 2d-2 항목7: 적분기 비교. 조화진동자(F(x)=-kx)에서 오래(20주기) 돌렸을 때
// 각 적분기의 에너지가 어떻게 거동하는지로 semi-implicit Euler가 지금
// stepRigidBody()에 쓰인 이유를 정량적으로 뒷받침한다.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "../src/physics/integrators.h"

namespace {

struct EnergyStats {
    float initial;
    float maxEnergy;
    float minEnergy;
    float finalEnergy;
};

float energyOf(const OscillatorState& s, float k, float m) {
    return 0.5f * k * s.position * s.position + 0.5f * m * s.velocity * s.velocity;
}

template <typename StepFn>
EnergyStats runAndTrackEnergy(StepFn step, float k, float m, float dt, int steps) {
    OscillatorState s{1.0f, 0.0f};  // 진폭 1에서 정지 상태로 시작
    EnergyStats stats{};
    stats.initial = energyOf(s, k, m);
    stats.maxEnergy = stats.initial;
    stats.minEnergy = stats.initial;
    for (int i = 0; i < steps; ++i) {
        step(s, k, m, dt);
        float e = energyOf(s, k, m);
        stats.maxEnergy = std::max(stats.maxEnergy, e);
        stats.minEnergy = std::min(stats.minEnergy, e);
    }
    stats.finalEnergy = energyOf(s, k, m);
    return stats;
}

}  // namespace

// Explicit Euler는 symplectic이 아니라서 진동계에서 에너지가 스텝마다
// (1+(w*dt)^2)배씩 계속 불어난다 -- 20주기(약 2500스텝) 돌리면 수백 배까지
// 발산해야 정상.
TEST(IntegratorComparison, ExplicitEulerEnergyDiverges) {
    constexpr float k = 1.0f, m = 1.0f, dt = 0.05f;
    constexpr float period = 2.0f * static_cast<float>(M_PI);  // w = sqrt(k/m) = 1
    const int steps = static_cast<int>(20.0f * period / dt);

    EnergyStats stats = runAndTrackEnergy(stepExplicitEuler, k, m, dt, steps);

    EXPECT_GT(stats.maxEnergy, stats.initial * 10.0f)
        << "explicit Euler energy did not diverge as expected on a harmonic oscillator";
}

// Semi-implicit Euler(현재 stepRigidBody()와 같은 방식)는 symplectic이라
// 같은 조건에서 에너지가 발산하지 않고 초기값 근처에서 유계로 남아야 한다.
TEST(IntegratorComparison, SemiImplicitEulerEnergyStaysBounded) {
    constexpr float k = 1.0f, m = 1.0f, dt = 0.05f;
    constexpr float period = 2.0f * static_cast<float>(M_PI);
    const int steps = static_cast<int>(20.0f * period / dt);

    EnergyStats stats = runAndTrackEnergy(stepSemiImplicitEuler, k, m, dt, steps);

    EXPECT_LT(stats.maxEnergy, stats.initial * 1.5f)
        << "semi-implicit Euler energy grew more than expected for a symplectic integrator";
    EXPECT_GT(stats.minEnergy, stats.initial * 0.5f)
        << "semi-implicit Euler energy dropped more than expected for a symplectic integrator";
}

// Velocity Verlet도 symplectic이라 semi-implicit Euler와 마찬가지로
// 유계여야 하고, 보통 더 정확해서 편차가 더 작아야 한다.
TEST(IntegratorComparison, VelocityVerletEnergyStaysBounded) {
    constexpr float k = 1.0f, m = 1.0f, dt = 0.05f;
    constexpr float period = 2.0f * static_cast<float>(M_PI);
    const int steps = static_cast<int>(20.0f * period / dt);

    EnergyStats stats = runAndTrackEnergy(stepVelocityVerlet, k, m, dt, steps);

    EXPECT_LT(stats.maxEnergy, stats.initial * 1.1f)
        << "velocity Verlet energy grew more than expected for a symplectic integrator";
    EXPECT_GT(stats.minEnergy, stats.initial * 0.9f)
        << "velocity Verlet energy dropped more than expected for a symplectic integrator";
}

// RK4는 국소 오차가 훨씬 작지만(4차) symplectic이 아니라서, explicit
// Euler처럼 발산하진 않되 semi-implicit Euler보다 이 dt/주기수에서 딱히
// 더 안 좋아지진 않아야 한다(장기적으로는 서서히 감쇠할 수 있음 -- 여기서는
// "explicit Euler처럼 폭주하지 않는다"만 확인).
TEST(IntegratorComparison, RK4EnergyDoesNotDiverge) {
    constexpr float k = 1.0f, m = 1.0f, dt = 0.05f;
    constexpr float period = 2.0f * static_cast<float>(M_PI);
    const int steps = static_cast<int>(20.0f * period / dt);

    EnergyStats stats = runAndTrackEnergy(stepRK4, k, m, dt, steps);

    EXPECT_LT(stats.maxEnergy, stats.initial * 1.5f)
        << "RK4 energy diverged more than expected";
}
