# terrain-sim

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

RL 환경·reward·physics 튜닝은 `reward-oob-50` 실험(실패 유형이 맵 이탈에서 시간초과로 전이되는 것만 확인, 성공률 개선 없음)을 마지막으로 동결했다. RL은 C++ 코어의 실제 workload이자 case study로 유지하되, 지금부터는 다시 C++ 물리 correctness test, 성능 계측, 병렬 stepping, 아키텍처 근거 문서화로 무게중심을 옮긴다.

## 구현된 기능

### C++ simulation core

- 라이브러리 없이 구현한 Perlin noise와 fBm heightmap
- talus-angle 기반 thermal erosion
- droplet 기반 hydraulic erosion
- `Heightmap::sample()`의 bilinear height/gradient sampling
- semi-implicit Euler 기반 최소 강체 운동
- 지형 법선을 이용한 충돌 응답과 접선 방향 중력·힘 투영
- 에너지 거동 검증으로 접촉 중 중력이 누락되던 실제 버그 탐지 및 수정
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

따라서 이 baseline은 terrain-aware routing보다 관성이 있는 goal navigation을 학습한 결과에 가깝다. 이후 `F_MAX=2.0`, `TALUS_ANGLE=0.15` 후보에서 직선 경로의 56%가 차단되면서도 모든 start-goal 쌍에 우회로가 존재하는 조건을 찾았다. 이 조건에서 나타난 맵 이탈(75%)을 줄이기 위해 out-of-bounds penalty를 5배로 올려 재학습했지만(`reward-oob-50`), 이탈이 준 만큼(-15%p) 시간초과가 늘었을 뿐(+20%p) 성공률은 개선되지 않아 기각했다. 이 결과를 마지막으로 reward/observation 튜닝을 동결하고, 지금은 C++ 물리 correctness test와 성능 계측·병렬화로 무게중심을 옮기는 중이다.

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
| 2c | 환경 동결(2026-08-01) + 물리 correctness test, throughput profiling, parallel stepping, 아키텍처 근거 문서화 | 진행 중 |
| 2d | Unity policy replay와 지원용 결과 정리 | replay 완료, 최종 결과 대기 |
| 2e | articulated robot extension | 2c 이후 검토 |
| 3 | automated tests, CI, English documentation, final demo | 예정 |

## 문서

- [`docs/evaluation-protocol.md`](docs/evaluation-protocol.md): 평가·실험·명명·plot 원칙
- [`docs/rl-bindings.md`](docs/rl-bindings.md): C++/Python 바인딩 설계
- [`docs/net-protocol.md`](docs/net-protocol.md): C++/Unity TCP 프로토콜
- [`benchmarks/README.md`](benchmarks/README.md): benchmark 아티팩트 구조
- [`docs/notes/`](docs/notes/): 구현 과정에서 정리한 C++·수학·시뮬레이션 학습 기록
