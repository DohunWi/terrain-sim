# `perf-parallel-envs` — Batched multi-env reset/step across `std::thread`

> 이 실험은 RL observation/reward/physics 튜닝이 아니라 `perf` 카테고리 성능 실험이다 (`docs/evaluation-protocol.md` §17 naming table의 `perf-parallel-envs` 예시 그대로). 템플릿의 결과 표(Success/Out-of-bounds/Timeout rate)는 RL episode 결과용이라 이 실험엔 적용되지 않는다 — 아래 "결과" 절에서 처리량(steps/sec, resets/sec) 표로 대체한다. AGENTS.md: "기존 구조를 재사용하고, 표현할 수 없을 때만 바꾸고 이유를 남긴다"에 따른 의도적 변형.

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-003` |
| Created | `2026-08-02` |
| Planned | `2026-08-02` |
| Started | `2026-08-02` |
| Completed | `2026-08-02` |
| Predecessor | `perf-stack-baseline` (아직 정식 실험 문서 없음 — commit `f46a87c`의 계측 결과, 아래 "근거" 참고) |

## 상태

`Inconclusive`

## 관찰

`perf-stack-baseline__bench-cpp.json`/`perf-stack-baseline__bench-python.json` (commit `f46a87c`) 계측 결과, `TerrainAgentEnv.reset()`은 평균 5.53ms인데 반해 `step()`은 평균 6.32us — 약 870배 차이. 실제 D20 평가(`env-maxsteps1000-fmax2__train-s0__eval-dev20__controller-ppo.json`)의 평균 episode 길이(462 step)로 환산하면 episode당 env 쪽 wall-clock의 약 65%가 reset 하나에 쓰인다. 현재 학습 루프는 env 1개를 순차 실행하므로, 이 reset 비용이 PPO rollout 수집 처리량의 실제 병목일 가능성이 높다.

## 근거

- Baseline 계측: [`perf-stack-baseline__bench-cpp.json`](../evaluations/perf-stack-baseline__bench-cpp.json), [`perf-stack-baseline__bench-python.json`](../evaluations/perf-stack-baseline__bench-python.json)
- reset 평균 5.53ms(Python `env_reset_full`) vs C++ native `fbm+thermal_erode(combined)` 5.53ms — reset 비용은 거의 전부 지형 생성/침식이고 Python 쪽 RNG start/goal 탐색은 무시할 수준.
- pybind11 콜 경계 자체의 오버헤드(`step_rigid_body` 바인딩 단독 1.02us vs native 0.02us, 약 50배)는 절대값이 작아 `env.step()` 전체(6.32us)에서 차지하는 비중은 작다 — 이번 실험의 핵심 동기는 콜 경계 절감이 아니라 **reset을 여러 env에 걸쳐 병렬로 겹치는 것**.
- 하드웨어: 개발 머신 물리 코어 8개, 논리 코어 8개(Apple Silicon, 하이퍼스레딩 없음), arm64, P/E 코어 비대칭 — 8개까지 선형 스케일링은 기대하지 않음.

## 가설

N개의 독립된 (`Heightmap`, `RigidBody`) 인스턴스를 `std::thread`로 병렬 reset/step하고 결과를 numpy 배열로 묶어 한 번의 pybind11 호출로 반환하면, 총 처리량(aggregate steps/sec, resets/sec)이 스레드 수 증가에 따라 늘어나되 물리 코어 수(8) 부근에서 스케일링이 꺾인다(오버서브스크립션 + P/E 코어 이질성).

## 독립변수

```text
thread_count (batch 워커 스레드 수): {1, 2, 4, 6, 8, 12, 16}
```

`thread_count == batch 내 env 개수`로 고정한다(스레드당 env 1개, 1:1 매핑). 이 실험은 "스레드 수 대비 env 수" 비율 자체를 변수로 다루지 않는다 — 그건 별도 질문.

## 통제변수

이 실험은 RL 튜닝이 아니므로 템플릿의 observation/reward/PPO/training 필드는 적용되지 않는다. 대신:

- physics/terrain/reward 설정: `env-maxsteps1000-fmax2` 기준으로 동결된 현재 값 그대로(`F_MAX=2.0`, `TALUS_ANGLE=0.15`, `MAX_STEPS=1000` 등) — 이 실험은 이 값들을 바꾸지 않는다.
- 학습(PPO) 자체는 실행하지 않는다 — 순수 env 엔진 처리량만 측정한다. PyTorch/SB3의 자체 스레드 풀과의 상호작용은 범위 밖(다음 질문으로 남김).
- 반복 횟수: `thread_count`별 5회 이상 반복 측정, 평균/분산 함께 기록(노이즈가 커서 1회 측정은 신뢰 불가 — `rigid_body_step`의 max가 median의 수십~수백 배 튀는 걸 baseline에서 이미 확인함).
- 하드웨어: 이 실험 문서를 실행하는 동일 개발 머신(다른 머신 결과와 직접 비교 금지).

## 사전 채택 기준

두 층위로 나눈다 — **정확성**이 선행 조건, **처리량**이 채택 여부.

### 정확성 (통과 못 하면 처리량과 무관하게 `Rejected`)

- 동일 seed에 대해 batched 실행 결과(`reset_batch`/`step_batch`)가 기존 순차 단일 스레드 실행과 **비트 단위로 동일**해야 한다(기존 `StepIsDeterministicForIdenticalInputs` 테스트와 같은 기준 — 병렬화가 부동소수점 연산 순서나 공유 상태 경합으로 결과를 바꾸면 안 됨).

### 처리량

물리 코어 수(8)에서의 처리량을 `thread_count=1` 대비 배수로 본다.

- **Accepted**: `thread_count=8`에서 aggregate steps/sec 및 resets/sec가 `thread_count=1` 대비 **4배 이상**이고, 스케일링 곡선이 단조 증가(오버서브스크립션 구간 전까지)한다.
- **Rejected**: `thread_count=8`에서 2배 미만이거나, thread_count 증가에 따른 개선이 거의 없다(std::thread 생성/조인 오버헤드나 공유 자원 경합이 이득을 상쇄) — 이 경우 스레드 풀 재사용 등 구현을 재검토하거나 병렬화 자체를 보류.
- **Inconclusive**: 2~4배 사이 — 병목이 실제로 무엇인지(스레드 생성 비용, 메모리 대역폭, PerlinNoise/erosion 내부의 숨은 공유 상태) 추가 조사 필요.

## 실행 정보

- 구현: `core/src/bench_batch.cpp` (신규 `bench_batch` CMake 타겟), `core/CMakeLists.txt`에 `Threads::Threads` 링크 추가.
- 실행 명령: `cd core && cmake --build build --target bench_batch && ./build/bench_batch benchmarks/evaluations/perf-parallel-envs__bench-cpp.json`
- 결과물: [`perf-parallel-envs__bench-cpp.json`](../evaluations/perf-parallel-envs__bench-cpp.json)
- **구현 중 방법론 수정 1건**: 최초 버전은 `thread_count`별 매 반복(7회)마다 `std::thread`를 새로 spawn/join했다. 첫 실행 결과 `steps/sec`가 thread_count에 따라 비단조적으로 튀었는데(`rigid_body_step` 1회가 ~0.02us라 1000 step 배치도 ~20us — 스레드 생성/조인 비용보다 작음), 이는 실제 스텝 처리량이 아니라 스레드 생성 오버헤드를 측정한 것이었다. `std::barrier` 기반의 영속적 워커 풀(`WorkerPool`, thread_count별로 한 번만 생성해 7회 반복 동안 재사용)로 교체하고, step phase 1회당 step 수도 1000 → 50,000(에피소드 50개분)으로 늘려 실측정을 다시 실행했다(SB3 rollout 수집도 실제로는 워커를 매 스텝 재생성하지 않으므로 이쪽이 현실적인 사용 패턴에도 더 가깝다).
- 정확성 게이트: 병렬(8-thread 풀, 200 step) vs 순차 실행 결과 비트 단위 비교 — **PASS**.

## 결과

| thread_count | resets/sec (mean±sd) | steps/sec (mean±sd) | speedup vs thread_count=1 |
|---:|---:|---:|---:|
| 1 | 177.6 ± 3.8 | 23,952,608 ± 588,930 | 1.00x |
| 2 | 250.3 ± 51.0 | 34,470,906 ± 9,761,917 | 1.44x |
| 4 | 408.3 ± 47.3 | 65,114,564 ± 12,685,542 | 2.72x |
| 6 | 555.6 ± 90.8 | 62,493,274 ± 10,550,827 | 2.61x |
| 8 | 523.2 ± 70.0 | 75,221,391 ± 13,060,379 | 3.14x |
| 12 | 575.1 ± 75.9 | 85,849,316 ± 19,686,320 | 3.58x |
| 16 | 653.4 ± 70.9 | 81,903,685 ± 24,547,432 | 3.42x |

물리 코어 수(8)에서 steps/sec 배수는 **3.14x**, resets/sec 배수는 177.6→523.2로 **2.95x** — 둘 다 사전 등록한 Accepted 기준(4배 이상)에 못 미치고 Rejected 기준(2배 미만)보다는 위, 즉 Inconclusive 구간이다. 분산도 상당히 크다(특히 12/16-thread resets/sec의 sd가 mean의 13~15%) — 반복 7회로는 노이즈가 완전히 가라앉지 않았다. thread_count=6에서 8로 갈 때 resets/sec가 오히려 소폭 하락(555.6→523.2)하는 비단조 구간도 있어, 물리 코어 8개 근방에서 뭔가(스케줄링, 메모리 대역폭)가 이미 스케일링을 방해하고 있을 가능성을 보여준다.

## 결론

`Inconclusive`

사전 등록 기준(물리 코어 8개에서 4배 이상 = Accepted, 2배 미만 = Rejected) 중 어느 쪽에도 해당하지 않는다. 스레드 생성 오버헤드(1차 구현의 문제)는 영속 풀 도입으로 배제했으므로, 남은 유력한 원인은 사전에 지목했던 후보 중 "PerlinNoise/erosion 내부 숨은 공유 상태"가 아니라 **힙 할당자 경합**으로 좁혀진다: `PerlinNoise` 생성자가 매 `resetSlot` 호출마다 256×256 gradient table(`gx_`/`gy_`, 각 256KB, 합 512KB)을 새로 `unique_ptr<float[]>`로 할당하는데, 이는 실제 필요한 64×64 heightmap 대비 과도하게 큰 테이블이고, 여러 스레드가 동시에 이 정도 크기를 반복 할당/해제하면 malloc arena 경합이 실제 CPU 병렬성을 갉아먹을 수 있다. 확정은 아니지만, 스레드 생성 비용을 배제한 뒤에도 스케일링이 4배에 못 미치는 것과 6→8 thread 구간의 비단조성 모두 이 가설과 일치한다.

**참고 (`docs/evaluation-protocol.md` §18 기준 재확인)**: 이 실험의 사전 채택 기준은 "4배 문턱"이라는 목표 크기(magnitude) 판단이라, thread_count=1과 8을 baseline/candidate로 놓고 §18의 통계 검정을 적용하는 게 원래 질문("thread_count=8이 4배 이상이냐")과는 다르다. 참고 삼아 계산하면 resets/sec `t=13.04`(`pct=+194.6%`), steps/sec `t=17.36`(`pct=+214.0%`) — 병렬화 자체의 효과는 노이즈로 설명 불가능할 만큼 확실하다(당연히 그렇다: 스레드 8개가 실제로 동시에 일한다). `Inconclusive` 판정은 "병렬화가 실재하는가"가 아니라 "4배라는 목표치에 도달했는가"에 대한 것이며, 이후 실험(EXP-004~006)이 그 미달의 원인을 좁혀왔다.

## 다음 질문

`PerlinNoise`의 gradient table 재할당(512KB/reset)이 실제로 스케일링을 제한하는 힙 경합의 원인인가? — 확인 방법: (1) `PerlinNoise` 인스턴스를 스레드별로 한 번만 만들고 여러 reset에서 재사용하도록 바꾼 변형으로 같은 스윕을 다시 돌려 resets/sec가 4배 문턱을 넘는지 확인, 또는 (2) 힙 프로파일러(`heaptrack`/`malloc_history`)로 reset phase 중 할당 횟수/경합을 직접 측정. 이건 `PerlinNoise`의 테이블 크기/재사용 정책을 바꾸는 것이라 알고리즘이 아니라 자료구조 수명 관리 문제다([`perf-perlin-table-reuse`](perf-perlin-table-reuse.md), EXP-004).

이 결과와는 별개로, PyTorch/SB3 학습 프로세스와 동시에 돌릴 때도 같은 스케일링이 유지되는지(스레드 예산 조율 필요 여부)는 여전히 미확인 — 위 힙 경합 질문을 해소한 뒤에 이어서 볼 문제다.
