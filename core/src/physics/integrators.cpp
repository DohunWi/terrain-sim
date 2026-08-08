#include "integrators.h"

namespace {
float acceleration(float x, float k, float m) {
    return -(k / m) * x;
}
}  // namespace

void stepExplicitEuler(OscillatorState& s, float k, float m, float dt) {
    float a = acceleration(s.position, k, m);
    float newVelocity = s.velocity + a * dt;
    s.position = s.position + s.velocity * dt;  // 옛 velocity로 위치 갱신
    s.velocity = newVelocity;
}

void stepSemiImplicitEuler(OscillatorState& s, float k, float m, float dt) {
    float a = acceleration(s.position, k, m);
    s.velocity = s.velocity + a * dt;
    s.position = s.position + s.velocity * dt;  // 새 velocity로 위치 갱신
}

void stepVelocityVerlet(OscillatorState& s, float k, float m, float dt) {
    float aOld = acceleration(s.position, k, m);
    s.position = s.position + s.velocity * dt + 0.5f * aOld * dt * dt;
    float aNew = acceleration(s.position, k, m);
    s.velocity = s.velocity + 0.5f * (aOld + aNew) * dt;
}

void stepRK4(OscillatorState& s, float k, float m, float dt) {
    struct Derivative {
        float dx;
        float dv;
    };
    auto f = [k, m](float x, float v) -> Derivative { return {v, acceleration(x, k, m)}; };

    Derivative k1 = f(s.position, s.velocity);
    Derivative k2 = f(s.position + 0.5f * dt * k1.dx, s.velocity + 0.5f * dt * k1.dv);
    Derivative k3 = f(s.position + 0.5f * dt * k2.dx, s.velocity + 0.5f * dt * k2.dv);
    Derivative k4 = f(s.position + dt * k3.dx, s.velocity + dt * k3.dv);

    s.position = s.position + (dt / 6.0f) * (k1.dx + 2.0f * k2.dx + 2.0f * k3.dx + k4.dx);
    s.velocity = s.velocity + (dt / 6.0f) * (k1.dv + 2.0f * k2.dv + 2.0f * k3.dv + k4.dv);
}
