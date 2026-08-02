# `perf-perlin-table-reuse` — `PerlinNoise` gradient-table alloc-once vs alloc-per-reset

> `perf` 카테고리 성능 실험. 템플릿의 RL 결과 표(Success/Out-of-bounds/Timeout rate)는 적용되지 않는다 — "결과"는 [`perf-parallel-envs`](perf-parallel-envs.md)와 같은 처리량(steps/sec, resets/sec) 표 형식을 그대로 재사용한다.

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-004` |
| Created | `2026-08-02` |
| Planned | `2026-08-02` |
| Started | `2026-08-02` |
| Completed | `2026-08-02` |
| Predecessor | `perf-parallel-envs` |

## 상태

`Rejected`

## 관찰

[`perf-parallel-envs`](perf-parallel-envs.md) 스윕에서 물리 코어 수(8)의 처리량 배수가 steps/sec 3.14x, resets/sec 2.95x로 사전 등록한 Accepted 기준(4배 이상)에 못 미쳐 `Inconclusive`로 종료됐다. thread_count 6→8 구간에서 resets/sec가 비단조적으로 하락(555.6 → 523.2)하는 것도 관찰됐다 — 스레드 생성 오버헤드는 이미 영속 워커 풀 도입으로 배제한 뒤의 결과라, 남은 유력 후보는 스레드 간 힙 할당자 경합이다.

## 근거

- `core/src/noise/perlin_noise.cpp:7-21` — `PerlinNoise` 생성자가 매 호출마다 `gx_`/`gy_` 각각 `TABLE_SIZE*TABLE_SIZE`(256×256) `float`, 즉 256KB×2=512KB를 `std::make_unique<float[]>`로 새로 할당한다. `TerrainAgentEnv.reset()`은 매 episode마다 새 `PerlinNoise(seed)`를 만드므로, 이 512KB 할당/해제가 reset마다(스레드마다 동시에) 반복된다.
- 메커니즘: 범용 allocator는 "맞는 크기의 빈 블록을 찾는" free-list 탐색/북키핑을 스레드 간 공유 자료구조(락 있는 arena)에서 수행하는 경우가 많고, 512KB는 스레드별 fast-path 캐시(보통 수십~수백 바이트대까지만 커버)를 벗어날 가능성이 높은 크기다 — 이게 맞다면 스레드 수가 늘어도 이 구간에서 직렬화가 생겨 처리량이 선형으로 안 늘어난다.
- `perf-parallel-envs__bench-cpp.json`의 원시 수치 (baseline).

## 가설

`PerlinNoise`의 할당(생성자에서 하는 `make_unique<float[]>` 두 번)과 "재시드"(RNG로 테이블 값 채우기)를 분리해서, 테이블을 스레드당 1회만 할당하고 이후 매 reset마다 값만 다시 채우면(`reseed(seed)`), 힙 할당/해제 빈도가 스레드 수·reset 횟수와 무관하게 사라져서 8-thread 처리량 배수가 `perf-parallel-envs`의 3.14x(steps/sec)/2.95x(resets/sec)보다 유의하게 개선된다.

## 독립변수

```text
PerlinNoise 테이블 할당 정책:
  baseline  = 매 reset마다 새 PerlinNoise(seed) 생성 (현재 코드, perf-parallel-envs와 동일)
  candidate = 워커(스레드)당 PerlinNoise 인스턴스 1개를 최초 1회만 생성,
              이후 reset마다 reseed(seed)로 gx_/gy_ 값만 다시 채움 (재할당 없음)
```

## 통제변수

- thread_count 그리드: `{1, 2, 4, 6, 8, 12, 16}` (perf-parallel-envs와 동일)
- 반복 횟수: thread_count당 7회 (perf-parallel-envs와 동일)
- heightmap/terrain 상수: `MAP_SIZE=64`, `SCALE=10.0`, `OCTAVES=3`, `PERSISTENCE=0.5`, `LACUNARITY=2.0`, `TALUS_ANGLE=0.15`, `EROSION_RATE=0.3`, `EROSION_ITERATIONS=10` (env.py/perf-parallel-envs와 동일, 불변)
- 물리 상수: `DT=1/60`, 힘/중력 벡터 동일, step phase 규모(에피소드 50개분=50,000 step) 동일
- `TABLE_SIZE=256`, `noise2D`/`fbm` 수식, gradient 값 생성 RNG 시퀀스(`std::mt19937(seed)` + `uniform_real_distribution`) — **변경 없음**. `reseed(seed)`는 생성자와 동일한 순서로 동일한 RNG를 돌려 동일한 값을 채워야 한다 (baseline과 candidate가 같은 seed에 대해 같은 지형을 만들어야 비교가 성립).
- 하드웨어: `perf-parallel-envs`와 동일한 개발 머신에서 같은 세션 내 측정(머신 간 비교 금지).

## 사전 채택 기준

### 정확성 (선행 조건, 실패 시 처리량과 무관하게 `Rejected`)

- `reseed(seed)`로 채운 candidate의 지형이 동일 seed에 대해 baseline(생성자 방식)과 **비트 단위로 동일**해야 한다.
- `perf-parallel-envs`와 동일한 병렬=순차 비트 동일성 correctness gate도 다시 통과해야 한다.

### 처리량

`perf-parallel-envs`의 8-thread 배수(steps/sec 3.14x, resets/sec 2.95x)를 baseline으로 candidate의 개선폭을 본다.

- **Accepted**: 8-thread에서 steps/sec, resets/sec 배수가 모두 baseline 대비 **+50%p 이상** 개선(즉 steps/sec ≥4.71x, resets/sec ≥4.43x) — allocator 경합 가설을 지지하는 것으로 판단.
- **Rejected**: 두 지표 모두 +15%p 미만 개선 — 가설 기각, 병목은 할당자 경합이 아닌 다른 요인(메모리 대역폭, cache-line 경합, OS 스케줄링 등)일 가능성이 높음.
- **Inconclusive**: 그 사이, 또는 두 지표가 서로 다른 방향을 가리키는 경우.

## 실행 정보

- 구현: `core/src/noise/perlin_noise.h`/`.cpp`에 `reseed(unsigned seed)` 추가 — 생성자에서 하던 RNG 채우기 루프를 분리했다. 생성자 시그니처가 `PerlinNoise(unsigned)` → `PerlinNoise()`로 바뀌면서 깨진 5개 호출부(`main.cpp`, `tune_cli.cpp`, `physics_test.cpp` 2곳, `bench.cpp`, `bench_batch.cpp`, `py_bindings.cpp`)는 `PerlinNoise noise; noise.reseed(seed);`로 맞춰 고쳤다.
- 벤치마크 하네스: `core/src/bench_batch_perlin_reuse.cpp` (신규 `bench_batch_perlin_reuse` CMake 타겟) — `perf-parallel-envs`의 `bench_batch.cpp`와 같은 `std::barrier` 영속 워커 풀 구조를 재사용하고, baseline(매 reset마다 로컬 `PerlinNoise` 새로 생성)과 candidate(`EnvSlot`에 상주하는 `PerlinNoise`를 `reseed()`만 호출) 두 경로를 같은 스윕으로 비교.
- 실행 명령: `cd core && cmake --build build --target bench_batch_perlin_reuse && ./build/bench_batch_perlin_reuse benchmarks/evaluations/perf-perlin-table-reuse__bench-cpp.json`
- 결과물: [`perf-perlin-table-reuse__bench-cpp.json`](../evaluations/perf-perlin-table-reuse__bench-cpp.json)
- 정확성 게이트: (1) candidate `reseed()` 결과가 동일 seed에 대해 baseline 결과와 비트 단위 동일 — **PASS**. (2) 병렬=순차 비트 동일성, baseline/candidate 각각 — **PASS**.

## 결과

| thread_count | resets/sec baseline | resets/sec candidate | steps/sec baseline | steps/sec candidate |
|---:|---:|---:|---:|---:|
| 1 | 113.9 ± 3.3 | 117.8 ± 15.3 | 21,299,949 ± 3,990,530 | 19,783,632 ± 3,552,211 |
| 2 | 244.9 ± 53.6 | 266.5 ± 40.8 | 30,784,554 ± 10,945,693 | 42,703,908 ± 8,527,462 |
| 4 | 386.5 ± 51.5 | 363.9 ± 47.0 | 52,297,829 ± 8,129,569 | 40,487,387 ± 8,867,274 |
| 6 | 517.5 ± 71.1 | 460.9 ± 80.6 | 63,335,026 ± 11,321,685 | 58,546,599 ± 6,772,230 |
| 8 | 580.6 ± 84.3 | 441.2 ± 61.6 | 72,660,699 ± 15,455,446 | 61,754,947 ± 11,806,451 |
| 12 | 598.8 ± 97.2 | 377.7 ± 109.6 | 82,639,729 ± 12,447,838 | 58,580,009 ± 24,778,934 |
| 16 | 614.9 ± 90.0 | 443.9 ± 138.1 | 72,720,096 ± 16,884,457 | 66,470,035 ± 14,921,831 |

thread_count=8에서 resets/sec 변화 **-24.0%**, steps/sec 변화 **-15.0%** — 둘 다 개선이 아니라 **악화**다. thread_count 4 이상 전 구간에서 candidate가 baseline보다 낮거나 비슷한 처리량을 보이고, 12-thread에서는 격차가 가장 크다(598.8 → 377.7, -37%). baseline과 정확히 같은 seed로 정확히 같은 지형을 만든다는 correctness gate는 통과했으므로, 이 차이는 결과의 정확성이 아니라 순수하게 메모리 레이아웃/실행 특성 변화에서 온 것이다.

## 결론

`Rejected`

사전 등록 당시엔 "+50%p 이상 개선/+15%p 미만 개선"이라는 라운드 넘버 기준을 썼다. `docs/evaluation-protocol.md` §18의 통계적 기준(반복 7회의 표준편차로 `t = Δ/SE_diff` 계산, `|t|>=2.5`이고 방향이 개선이며 `|pct_change|>=10%`면 Accepted, `|t|<1.0`이면 Rejected, 그 사이는 Inconclusive)으로 thread_count=8 지표를 다시 판정하면 지표별로 갈린다:

- **resets/sec**: `t=-3.53`, `pct=-24.0%` — `|t|>=2.5`이고 방향이 악화이므로 **Rejected(통계적으로 유의한 악화)**.
- **steps/sec**: `t=-1.48`, `pct=-15.0%` — `1.0<=|t|<2.5` 구간이라 **Inconclusive**(점 추정치는 나빠 보이지만 반복 7회의 노이즈로 통계적으로 확실히 구별되진 않는다).

resets/sec가 통계적으로 유의하게 나빠진 것 하나만으로도 "할당 경합이 병목이고 재사용이 도움이 된다"는 가설은 기각하기에 충분하다 — steps/sec의 불확실성은 가설을 살리는 근거가 아니라, 애초에 재사용의 이득이 있었어도 이 정도 반복 횟수로는 steps/sec에서 명확히 잡히지 않았을 것이라는 뜻이다.

원인 후보를 재고하면: `EnvSlot`에 `PerlinNoise reusableNoise` 멤버를 추가한 것 자체가 `EnvSlot`의 크기/정렬을 바꿨다. `std::vector<EnvSlot> slots_`는 연속 메모리이므로, 구조체 크기가 달라지면 인접한 슬롯들이 캐시 라인 경계에 걸리는 방식도 달라진다 — 여러 스레드가 각자 다른 `EnvSlot`을 동시에 쓰는데 그 슬롯들이 같은 캐시 라인을 공유하면(false sharing), 캐시 라인이 스레드 사이에서 계속 무효화되며 핑퐁쳐서 오히려 병렬성이 나빠질 수 있다. 이게 할당 경합보다 더 그럴듯한 설명이다 — 할당이 사라졌는데도 candidate가 baseline보다 느려진 걸 "할당 비용 감소"만으로는 설명할 수 없고, "구조체 레이아웃이 바뀌어 캐시 경합이 늘었다"는 설명은 정확히 이 방향(추가할수록 나빠짐)과 일치한다.

## 다음 질문

`EnvSlot`들 사이의 false sharing이 실제 원인인가? — 확인 방법: `EnvSlot`을 `alignas(64)`(전형적인 캐시 라인 크기)로 패딩해서 슬롯마다 독립된 캐시 라인을 갖게 만든 뒤, `perf-parallel-envs`(EXP-003)의 원래 baseline 스윕을 다시 돌려 처리량이 바뀌는지 확인한다([`perf-envslot-cache-align`](perf-envslot-cache-align.md), EXP-005).

이 결과와 별개로, EXP-003의 원래 관찰(6→8 thread 구간 비단조성, 8-thread 배수가 4x 문턱에 못 미침)은 아직 설명되지 않은 채로 남아 있다 — 이번 실험은 그 원인 후보 하나(할당 경합)를 배제했을 뿐이다.

**후속 계획**: 이 실험에서 `EnvSlot`에 `reusableNoise` 멤버를 추가한 것 자체가 구조체 크기/정렬을 바꿔 [`perf-envslot-cache-align`](perf-envslot-cache-align.md)(EXP-005)이 조사하는 false sharing을 새로 만들었을 수 있다 — 즉 "할당 경합이 사라진 이득"이 "정렬 악화로 인한 손해"에 가려지거나 역전됐을 가능성. 두 변수(할당 정책, 구조체 정렬)가 이 실험 안에서 얽혀 있었으므로, EXP-005가 캐시 라인 정렬 문제를 해소(`Accepted`)하면, 그 정렬된 `EnvSlot` 위에서 이 실험의 candidate(할당 1회 + `reseed()`)를 **다시** 붙여 단독 효과를 재검증해야 한다. 이건 EXP-005 완료 후의 자연스러운 다음 실험이며, 아직 계획만 된 상태다(별도 experiment-id 부여 예정).
