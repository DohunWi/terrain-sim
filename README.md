# terrain-sim

## Overview

C++20 simulation core for procedural terrain, thermal/hydraulic erosion, and rigid-body terrain contact. Python (pybind11 → Gymnasium/PPO) evaluates the core; Unity visualizes terrain and replays recorded trajectories. The core builds and runs without either consumer.

Linked documents are in Korean; their benchmark tables, plots, and result JSON are language-neutral. Narrative walkthrough: [case study](https://dohun-wi-portfolio.vercel.app/work/terrain-sim).

### Layout

| Path | Contents |
|---|---|
| `core/` | C++20 core (CMake ≥ 3.20) — Perlin/fBm noise, erosion, physics, TCP server, pybind11 bindings |
| `core/tests/` | GoogleTest suite, 16 tests |
| `training/` | Gymnasium environment, PPO train/eval, terrain analysis |
| `unity-client/` | Visualization and trajectory replay only; no simulation math |
| `benchmarks/` | Pre-registered experiments, fixed-seed results, plots |
| `docs/` | Protocol and engineering rationale |

### Build and test

```bash
cd core
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

GoogleTest is fetched by CMake `FetchContent`; the first configure needs network. The `terrain_sim_py` binding target is built only when pybind11 is discoverable, and is skipped otherwise. CI builds `core/` and runs the suite on ubuntu-latest/GCC, plus a ThreadSanitizer job over the `bench_batch*` worker pool.

### Measured results

| Result | Value | Evidence |
|---|---|---|
| Env reset throughput scaling, 1→8 threads | 2.95x → 5.33x | [`docs/performance-engineering.md`](docs/performance-engineering.md) |
| fBm + thermal erosion runtime, `Heightmap::at()` inlined across TUs | −20.0% combined, −61.4% thermal-only | [`docs/phase2d2-engineering-evidence.md`](docs/phase2d2-engineering-evidence.md) |
| Max energy drift at `dt=1/60`, semi-implicit Euler vs. explicit Euler | 0.84% vs. 711.6% | [`docs/phase2d2-engineering-evidence.md`](docs/phase2d2-engineering-evidence.md) |
| Physics step throughput scaling | ~3.4x, unmoved by every intervention tried; treated as this machine's ceiling | [`docs/performance-engineering.md`](docs/performance-engineering.md) |

### Scope limits

- `stepRigidBody` has no friction or restitution. Normal-axis contact response is fully inelastic and tangential velocity is preserved.
- High-velocity terrain tunneling is reproduced by a paired repro/control test but is not reachable on generated terrain, and is not fixed.
- The RL environment is frozen (2026-08-01). Its recorded evaluation numbers predate a heightmap boundary fix and do not reproduce against current physics code.
- `training/` is intentionally outside CI scope while the RL loop is frozen.

---

## 개요

C++ 시뮬레이션 코어 위에 절차적 지형·침식·강체 물리를 구현하고, Python RL 학습 환경과 Unity 시각화 클라이언트를 연결하는 프로젝트다.

시뮬레이션 연산은 C++ 코어가 담당한다. Python은 Gymnasium/PPO 학습 계층, Unity는 파라미터 조작과 결과 재생을 위한 시각화 계층으로 분리했다.

> 게임 시뮬레이션(Unity/C#)에서 독립 C++ 시뮬레이션 코어로 — 인터페이스 설계, 수치 검증, 재현 가능한 평가와 성능 엔지니어링 과정을 증명한다.

## 현재 상태

다음 end-to-end 경로를 완성했다.

```text
C++ physics
  → pybind11
  → Gymnasium environment
  → stable-baselines3 PPO training
  → fixed-seed deterministic evaluation
  → trajectory JSON
  → Unity replay
```

RL 환경·reward·physics 튜닝은 `reward-oob-50` 실험(실패 유형이 맵 이탈에서 시간초과로 전이되는 것만 확인, 성공률 개선 없음)을 마지막으로 동결했다. RL은 C++ 코어의 실제 workload이자 case study로 유지하고, 이후 무게중심은 C++ 물리 correctness test, 성능 계측, 병렬 stepping, 아키텍처 근거 문서화로 옮겼다 — 이 작업은 완료됐다.

성능 계측 결과 `env.reset()`이 `env.step()`보다 약 870배 비싸다는 걸 발견했고(`docs/performance-engineering.md`), 7건의 pre-registered 실험(EXP-003~008)을 거쳐 실제 병목이 스레드 확장성이 아니라 침식 알고리즘의 셀당 힙 할당(reset 1회당 40,960번)이었음을 확인했다. 이걸 고치고 캐시 라인 정렬을 더한 결과 물리 코어 8개 기준 reset 처리량 스케일링이 2.95배 → 5.33배로 개선됐다(원래 목표였던 4배 초과 달성). 물리 스텝 처리량은 이 개입들과 무관하게 계속 ~3.4배 근방이라, 이건 이 개발 머신의 하드웨어 병렬성 한계로 결론지었다.

C++ 코어는 GoogleTest 기반 correctness test(에너지 보존, slope-threshold, determinism, heightmap 경계, 침식 리팩토링 bit-exactness)를 갖췄고, `.github/workflows/ci.yml`이 push/PR마다 `core/` 전체 CMake 트리를 빌드하고 이 테스트를 GCC(ubuntu-latest)에서 실행한다 — 로컬(AppleClang)에서는 통과하던 코드가 CI 첫 실행에서 표준 라이브러리 구현체 차이로 인한 누락 include를 실제로 잡아냈다.

이 기반 위에 여섯 가지 심화 검증을 추가했다. 고속 물체가 지형을 통과하는 CCD 터널링은 구조적으로는 재현되지만 현재 생성 지형의 경사 범위에서는 발동하지 않음을 회귀 테스트로 확인했다. 경사 불연속 지형에서 dt를 1/240~1/5까지 훑은 고정 Δt 안정성 검사에서는 sub-stepping이 필요하지 않다는 결론이 나왔고, 이 sweep 도중 heightmap 경계 밖 쿼리가 gradient 보간을 오염시키던 실제 버그를 찾아 수정했다. `Heightmap::at()`을 `.cpp`에서 header로 옮겨 cross-TU inline을 가능하게 한 결과 fBm + thermal erosion 결합 실행 시간이 평균 20.0% 줄었다 — SIMD 벡터화 자체는 이 워크로드에서 이득이 없다는 걸 확인하는 과정에서 찾아낸 별개의 병목이었다. 4종 적분기(Explicit/Semi-implicit Euler, Velocity Verlet, RK4) dt sweep으로 현재 semi-implicit Euler 선택의 수치적 근거를 확보했고, `bench_batch*` 워커 풀은 Linux/GCC ThreadSanitizer CI에서 race가 검출되지 않았다. 측정 환경, 원시 수치, 한계는 [`docs/phase2d2-engineering-evidence.md`](docs/phase2d2-engineering-evidence.md)에 있다.

## 구현된 기능

### C++ simulation core

- 라이브러리 없이 구현한 Perlin noise와 fBm heightmap
- talus-angle 기반 thermal erosion
- droplet 기반 hydraulic erosion
- `Heightmap::sample()`의 bilinear height/gradient sampling
- semi-implicit Euler 기반 최소 강체 운동
- 지형 법선을 이용한 충돌 응답과 접선 방향 중력·힘 투영
- 에너지 거동 검증으로 접촉 중 중력이 누락되던 실제 버그 탐지 및 수정
- heightmap 경계 밖 쿼리에서 height는 clamp되지만 gradient는 그대로 외삽되던 버그 탐지 및 수정
- 고속 이동체의 CCD 지형 터널링 재현/대조 회귀 테스트 (현재 생성 지형 범위에서는 미발동)
- `Heightmap::at()` cross-TU inline으로 thermal erosion 실행 시간 개선
- Explicit/Semi-implicit Euler, Velocity Verlet, RK4 적분기 비교와 에너지 drift 벤치마크
- `bench_batch*` 워커 풀에 대한 Linux/GCC ThreadSanitizer CI 검증
- Unity용 TCP request-response 및 erosion snapshot 전송

### RL training environment

- pybind11 인프로세스 C++ 바인딩
- Gymnasium observation/action/reward/termination 계약
- episode마다 지형과 start/goal을 재생성하는 domain randomization
- 방향에 무관한 최대 힘을 위한 2D action L2 norm 제한
- stable-baselines3 PPO 학습과 TensorBoard logging
- 고정 seed deterministic 평가
- Random/Direct-to-goal/PPO controller baseline
- episode별 결과·경사·trajectory 구조화 저장

### Unity client

- C++ TCP 서버의 heightmap snapshot을 실시간 mesh로 변환
- 침식 파라미터 UI와 orbit camera
- 학습된 정책의 복수 episode trajectory 재생
- 성공·맵 이탈·시간초과 episode 선택 UI

## 아키텍처

```text
                         pybind11 (in-process)
                  ┌─────────────────────────────┐
                  ▼                             │
┌─────────────────────────┐          ┌──────────────────────┐
│ C++ Core                │          │ Python Training      │
│                         │          │                      │
│ terrain / erosion       │          │ Gymnasium environment│
│ rigid-body physics      │          │ PPO train/evaluate   │
│ TCP server              │          │ trajectory export    │
└────────────┬────────────┘          └──────────┬───────────┘
             │ TCP heightmap                    │ JSON replay
             ▼                                  ▼
       ┌────────────────────────────────────────────┐
       │ Unity Client                               │
       │ live terrain visualization / policy replay │
       └────────────────────────────────────────────┘
```

용도에 따라 연결 경로를 분리했다.

- Unity live visualization: 신뢰성이 필요한 request-response TCP
- RL training: step 처리량을 위한 pybind11 인프로세스 호출
- Policy visualization: 완료된 episode를 재생하는 정적 JSON

## 검증과 현재 발견

평가는 즉석 random seed가 아니라 고정 집합으로 분리한다.

| 집합 | Seed | 용도 |
|---|---:|---|
| D20 | `1000~1019` | 빠른 개발 평가 |
| V100 | `2000~2099` | 후보 비교 |
| T100 | `3000~3099` | 최종 설정 선택 후 한 번만 사용하는 평가 |

`baseline-l2cap-fmax5`의 V100 결과:

| Controller | 성공 | 맵 이탈 | 시간초과 |
|---|---:|---:|---:|
| Random | 0% | 18% | 82% |
| Direct-to-goal | 33% | 63% | 4% |
| PPO | 64% | 24% | 12% |

이 결과만 보면 PPO가 단순 controller보다 우수하지만, 지형 분석에서 중요한 한계가 드러났다.

- 당시 최대 등반각: `atan(F_MAX / mg) = 27.0°`
- 100개 지형의 최대 경사: `14.3°`
- 등반 불가능한 셀과 차단된 직선 경로: `0%`

따라서 이 baseline은 terrain-aware routing보다 관성이 있는 goal navigation을 학습한 결과에 가깝다. 이후 `F_MAX=2.0`, `TALUS_ANGLE=0.15` 후보에서 직선 경로의 56%가 차단되면서도 모든 start-goal 쌍에 우회로가 존재하는 조건을 찾았다. 이 조건에서 나타난 맵 이탈(75%)을 줄이기 위해 out-of-bounds penalty를 5배로 올려 재학습했지만(`reward-oob-50`), 이탈이 준 만큼(-15%p) 시간초과가 늘었을 뿐(+20%p) 성공률은 개선되지 않아 기각했다. 이 결과를 마지막으로 reward/observation 튜닝을 동결했다(이후 C++ 코어 쪽 작업은 위 '현재 상태' 참고).

이 표의 수치는 각 결과 JSON의 `eval_commit_sha`가 가리키는 대로 2026-08-01 heightmap 경계 clamp 수정 **이전**에 측정한 것이다. 당시 지배적 실패 유형이던 맵 이탈이 정확히 그 수정이 건드린 경계 구간에서 나왔으므로, 현재 물리 코드에서 동일 수치가 그대로 재현되지는 않는다. 동결 이후 재측정은 새 사전 등록 실험에 해당해 수행하지 않았다 — 이 표에서 끌어낸 결론은 절대 수치가 아니라 실패 유형의 분포에 근거한다.

성공률이나 training reward만으로 개선을 주장하지 않는다. 실험은 다음 순서를 따른다.

```text
관찰 → 근거 → 가설 → 사전 채택 기준 → 단일 변수 변경
→ D20 → 필요 시 V100 → 채택/기각
```

전체 평가·실험·명명·plot 원칙은 [`docs/evaluation-protocol.md`](docs/evaluation-protocol.md)에 있다.

## 저장소 구조

```text
core/                       C++20 simulation core (CMake)
  src/noise/                Perlin / fBm
  src/erosion/              thermal / hydraulic erosion
  src/physics/              integration and terrain contact
  src/net/                  Unity TCP server and protocol
  src/bindings/             pybind11 module
training/                   Gymnasium, PPO train/eval, terrain analysis
unity-client/               Unity visualization and trajectory replay
benchmarks/
  evaluations/              structured fixed-seed results
  experiments/              hypotheses, controls, results, decisions
  plots/                    reproducible public figures
docs/                       protocol and engineering rationale
```

PPO 모델, TensorBoard raw log와 checkpoint는 `training/artifacts/`에 로컬로 보관하고 Git에는 포함하지 않는다. 구조화된 평가 결과와 실험 결론만 commit한다.

## 빌드

### C++ core

```bash
cd core
cmake -S . -B build
cmake --build build
./build/terrain_sim_core
```

### 테스트

빌드한 뒤 같은 디렉터리에서:

```bash
ctest --test-dir build --output-on-failure
```

GoogleTest는 CMake `FetchContent`로 자동으로 내려받으므로 최초 configure에 네트워크가 필요하다.

### Python binding

pybind11을 사용할 Python 환경을 활성화한 뒤:

```bash
cd core
cmake -S . -B build
cmake --build build --target terrain_sim_py
```

### RL 학습과 평가

상세 명령과 로컬 Python 환경은 [`training/README.md`](training/README.md)를 참고한다.

```bash
cd training
python3 train.py --timesteps 1000000 --seed 0 \
  --out artifacts/<experiment-id>/model \
  --log-dir artifacts/<experiment-id>/logs \
  --tb-log-dir artifacts/<experiment-id>/tb_logs \
  --run-name <experiment-id> \
  --experiment-id <experiment-id>
```

## 로드맵

| Phase | 산출물 | 상태 |
|---|---|---|
| 0 | C++ thermal erosion console demo | 완료 |
| 1 | hydraulic erosion + TCP + Unity live visualization | 완료 |
| 2a | 최소 강체 물리 + 지형 충돌 + 에너지 검증 | 완료 |
| 2b | pybind11 + Gymnasium + PPO + fixed-seed evaluation | 완료 |
| 2c | 환경 동결(2026-08-01) + 물리 correctness test, throughput profiling, parallel stepping, 아키텍처 근거 문서화 | 완료 |
| 2d-1 | Unity policy replay | 완료 |
| 2d-2 | correctness/성능 심화 — CCD 지형 터널링 검증, 고정 Δt/sub-stepping 안정성, heightmap gradient 경계 버그 수정, `Heightmap::at()` inline 성능 개선, 워커 풀 ThreadSanitizer, 적분기(Euler/Verlet/RK4) 비교. 근거: [`docs/phase2d2-engineering-evidence.md`](docs/phase2d2-engineering-evidence.md) | 완료 |
| 2e | articulated robot extension | 2d 이후 검토 |
| 3 | English documentation, final demo | 예정 |

## 문서

- [`docs/evaluation-protocol.md`](docs/evaluation-protocol.md): 평가·실험·명명·plot 원칙, perf 실험 통계적 판단 기준
- [`docs/performance-engineering.md`](docs/performance-engineering.md): Phase 2c 성능 실험 전체 요약과 before/after 근거
- [`docs/phase2d2-engineering-evidence.md`](docs/phase2d2-engineering-evidence.md): Phase 2d-2 correctness/성능 심화 근거 — 측정 환경, 실험 수치, 판단표
- [`docs/rl-bindings.md`](docs/rl-bindings.md): C++/Python 바인딩 설계
- [`docs/net-protocol.md`](docs/net-protocol.md): C++/Unity TCP 프로토콜
- [`benchmarks/README.md`](benchmarks/README.md): benchmark 아티팩트 구조
- [`docs/notes/`](docs/notes/): 구현 과정에서 정리한 C++·수학·시뮬레이션 학습 기록
