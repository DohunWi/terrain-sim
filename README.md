# terrain-sim

C++ 시뮬레이션 코어 + Unity 시각화 클라이언트.
절차적 지형(heightmap) 위에 수력·열 침식을 시뮬레이션하고, 이후 강체 물리(구/상자 낙하 및 충돌)로 확장한다. 시뮬레이션 연산은 전부 C++ 코어가 담당하며, Unity는 시각화·인터랙션 클라이언트로만 사용한다.

> "게임 시뮬레이션(Unity/C#)에서 시뮬레이션 코어(C++)로 — 언어와 층위를 바꿔도 동일한 인터페이스 설계와 성능 엔지니어링 사고를 적용."

## 상태

Phase 0 (C++ 진입 + 워밍업) 진행 중.

## 구조

```
core/           C++ 시뮬레이션 코어 (CMake)
  src/noise/    Perlin, fBm 지형 생성
  src/erosion/  thermal / hydraulic 침식
  src/physics/  적분기, 충돌 (Phase 2)
  src/net/      소켓 서버
  tests/        GoogleTest (Phase 3)
unity-client/   Unity 시각화 프로젝트
benchmarks/     성능 측정 결과, 스크립트
docs/           아키텍처, 알고리즘 노트
```

## 빌드

```
cd core
cmake -S . -B build
cmake --build build
./build/terrain_sim_core
```

## 로드맵

| 시점 | 산출물 |
|---|---|
| 7월 말 | 열 침식 콘솔 데모 (Phase 0) |
| 9월 초 | v0.5 데모 영상 (수력 침식 + Unity) |
| 10월 말 | 성능 리포트 + 강체 물리 |
| 11월 말 | v1.0: 영문 README, CI, 최종 영상 |

자세한 계획은 `docs/`에 정리 예정.
