# terrain-sim

C++ 시뮬레이션 코어 + Unity 시각화 클라이언트.
절차적 지형(heightmap) 위에 수력·열 침식을 시뮬레이션하고, 그 지형과 상호작용하는 강체 에이전트 물리(중력·충돌·힘 기반 제어) 및 강화학습 기반 제어로 확장한다. 시뮬레이션 연산은 전부 C++ 코어가 담당하며, Unity는 시각화·인터랙션 클라이언트로만 사용한다.

> "게임 시뮬레이션(Unity/C#)에서 시뮬레이션 코어(C++)로 — 언어와 층위를 바꿔도 동일한 인터페이스 설계와 성능 엔지니어링 사고를 적용."

## 상태

Phase 0(C++ 진입 + 열 침식 워밍업), Phase 1(수력 침식 + Unity 시각화)은 완료. 현재는 Phase 2a — 지형 위를 움직이는 최소 강체 에이전트(중력, 지형 충돌, 힘 기반 제어) 구현 중.

## 구조

```
core/           C++ 시뮬레이션 코어 (CMake)
  src/noise/    Perlin, fBm 지형 생성
  src/erosion/  thermal / hydraulic 침식
  src/physics/  강체 적분기, 지형 충돌, 힘 기반 제어 (Phase 2a)
  src/net/      소켓 서버
  src/bindings/ pybind11 — Python RL 학습 루프용 인프로세스 인터페이스 (Phase 2b, 예정)
  tests/        GoogleTest (Phase 3)
unity-client/   Unity 시각화 프로젝트
training/       Gymnasium 환경 + RL(stable-baselines3) 학습 스크립트 (Phase 2b/2c, 예정)
benchmarks/     성능 측정 결과, 스크립트
docs/           아키텍처, 알고리즘 노트 (docs/net-protocol.md 등)
```

## 빌드

```
cd core
cmake -S . -B build
cmake --build build
./build/terrain_sim_core
```

## 로드맵

| Phase | 산출물 | 상태 |
|---|---|---|
| 0 | 열 침식 콘솔 데모 | 완료 |
| 1 (v0.5) | 수력 침식 + Unity 시각화 | 완료 |
| 2a | 최소 강체 에이전트: 중력·지형 충돌·힘 기반 제어 | 진행 중 |
| 2b | pybind11 바인딩 + Gymnasium 환경 + RL(stable-baselines3) 학습 | 예정 |
| 2c | 성능 엔지니어링 (병렬 환경 스텝, 처리량 벤치마크) | 예정 |
| 2d | 관절형 다리 확장 (스트레치 목표) | 예정 |
| 3 | 영문 README, GoogleTest, CI | 예정 |

자세한 계획은 `docs/`에 정리 예정.
