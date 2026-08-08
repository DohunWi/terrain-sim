#pragma once

// 1D 조화진동자(F(x) = -kx) 위에서 여러 적분기의 에너지 드리프트를
// 비교하기 위한 독립 모듈. RigidBody/Heightmap과 무관하게 설계함 --
// 지형/충돌 로직이 섞이면 "적분 방식 차이"와 "충돌 응답 차이"가
// 뒤섞여서 순수 비교가 안 됨. 조화진동자는 KE<->PE가 계속 교환돼서
// 적분기별 장기 에너지 거동(발산/유계/감쇠)이 뚜렷하게 드러나는
// 교과서적 표준 비교 문제.
struct OscillatorState {
    float position;
    float velocity;
};

// 현재 stepRigidBody()의 공중 분기(rigid_body.cpp)와 같은 패턴: 속도를
// 먼저 갱신하고, 그 새 속도로 위치를 갱신(semi-implicit/symplectic).
void stepSemiImplicitEuler(OscillatorState& s, float k, float m, float dt);

// 옛 속도로 위치를 갱신(explicit/forward Euler) -- 같은 비용(힘 평가
// 1회)이지만 symplectic이 아니라서 진동계에서 에너지가 발산하는 경향.
void stepExplicitEuler(OscillatorState& s, float k, float m, float dt);

// 새 위치에서 가속도를 재평가해 옛/새 가속도 평균으로 속도를 갱신
// (힘 평가 2회). semi-implicit Euler보다 보통 더 정확하고 마찬가지로
// symplectic.
void stepVelocityVerlet(OscillatorState& s, float k, float m, float dt);

// 스텝 안에서 4개 지점(k1~k4)의 미분을 가중평균(힘 평가 4회). 국소
// 오차는 훨씬 작지만(4차) symplectic이 아니라서 장기적으로는 오히려
// 에너지가 서서히 새어나갈 수 있음.
void stepRK4(OscillatorState& s, float k, float m, float dt);
