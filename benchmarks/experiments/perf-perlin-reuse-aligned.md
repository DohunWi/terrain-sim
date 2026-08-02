# `perf-perlin-reuse-aligned` — `PerlinNoise` alloc-once vs alloc-per-reset, with `EnvSlot` layout held fixed

> `perf` 카테고리 성능 실험. RL 결과 표는 적용되지 않는다 — 처리량(steps/sec, resets/sec) 표 형식을 재사용한다.

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-006` |
| Created | `2026-08-02` |
| Planned | `2026-08-02` |
| Started | `2026-08-02` |
| Completed | `2026-08-02` |
| Predecessor | `perf-envslot-cache-align` |

## 상태

`Inconclusive`

## 관찰

[`perf-perlin-table-reuse`](perf-perlin-table-reuse.md)(EXP-004)는 `EnvSlot`에 `PerlinNoise` 재사용 멤버를 추가해서 힙 할당 자체를 없앴지만, thread_count=8에서 resets/sec -24%, steps/sec -15%로 **악화**됐다. 그 원인으로 지목된 것은 멤버 추가로 `EnvSlot`의 크기/정렬이 바뀌어 새로운 false sharing이 생겼을 가능성이다. [`perf-envslot-cache-align`](perf-envslot-cache-align.md)(EXP-005)에서 `alignas(64)`로 `EnvSlot`을 캐시 라인에 정렬했더니 실제로 resets/sec가 개선됐다(+21.7% @ 8-thread) — 다만 steps/sec는 거의 무반응이었다. 즉 EXP-004의 baseline과 candidate는 **구조체 레이아웃 자체가 서로 달랐다**(candidate만 멤버가 추가됨) — 할당 정책의 순수한 효과를 격리하지 못한 실험이었다.

## 근거

- `perf-perlin-table-reuse__bench-cpp.json`, `perf-envslot-cache-align__bench-cpp.json`의 원시 수치.
- EXP-004의 `EnvSlot`(baseline, 멤버 없음)과 candidate(`reusableNoise` 멤버 추가)는 `sizeof`가 서로 다르다 — 이게 독립변수(할당 정책)와 얽힌 두 번째 변수였다.

## 가설

`PerlinNoise` 멤버를 baseline/candidate 양쪽 `EnvSlot`에 **똑같이** 두고(baseline은 그 멤버를 안 쓰고 로컬 `PerlinNoise`를 새로 만들며, candidate만 그 멤버를 `reseed()`하는 방식으로 사용), 두 모드가 완전히 동일한 크기/정렬의 구조체를 쓰도록 고정하면 — 레이아웃이라는 교란 변수가 사라지므로, 힙 할당 제거의 순수한 효과가 드러난다. `EnvSlot` 자체는 `alignas(64)`로 정렬해 EXP-005에서 확인된 reset-phase 개선을 함께 유지한다.

## 독립변수

```text
PerlinNoise 할당 정책 (EnvSlot 타입/크기는 baseline·candidate 모두 동일하게 고정):
  baseline  = 매 reset마다 로컬 PerlinNoise를 새로 생성 (slot의 상주 PerlinNoise 멤버는 존재하되 미사용)
  candidate = slot에 상주하는 PerlinNoise 멤버를 reseed(seed)로 재사용 (재할당 없음)
```

## 통제변수

- `EnvSlot` 타입: `alignas(64)`, `Heightmap` + `RigidBody` + `PerlinNoise` 멤버를 모두 포함 — baseline/candidate 두 모드에서 **동일한 타입**(같은 `sizeof`/`alignof`)을 사용한다. baseline 모드에서 그 멤버는 단순히 미사용 공간으로 남는다.
- thread_count 그리드, 반복 횟수(7회), heightmap/terrain/physics 상수, step phase 규모(에피소드 50개분): 이전 실험들과 동일.
- 워커 풀 동기화: `std::barrier` 영속 풀 (EXP-003/004/005와 동일 설계).

## 사전 채택 기준

### 정확성 (선행 조건)

- candidate가 동일 seed에 대해 baseline과 비트 단위로 동일한 지형을 만들어야 한다.
- 병렬=순차 비트 동일성, baseline/candidate 각각.

### 처리량

이번 실험 내에서 baseline과 candidate를 직접 비교한다(둘 다 같은 `EnvSlot` 타입을 쓰므로 레이아웃 차이라는 교란 변수는 없다).

- **Accepted**: thread_count=8에서 candidate의 resets/sec, steps/sec가 **모두 +20% 이상** baseline보다 개선 — 할당 제거가 레이아웃 교란 없이도 실질적 이득이라는 뜻.
- **Rejected**: 두 지표 모두 baseline 대비 ±10%p 이내(개선도 악화도 아님) 또는 악화 — 할당 자체는 애초에 병목이 아니었다는 뜻, EXP-004의 원래 결론(할당 경합 가설 기각)이 재확인됨.
- **Inconclusive**: 그 사이, 또는 두 지표가 다른 방향.

## 실행 정보

- 구현: `core/src/bench_batch_perlin_reuse_aligned.cpp` (신규 `bench_batch_perlin_reuse_aligned` CMake 타겟) — 단일 `alignas(64) EnvSlot` 타입(상주 `PerlinNoise noise` 멤버 포함, baseline 모드는 이 멤버를 안 쓰고 로컬 `PerlinNoise`를 새로 만듦)에 baseline/candidate 두 reset 함수를 붙여 같은 스윕에서 비교.
- 실행 명령: `cd core && cmake --build build --target bench_batch_perlin_reuse_aligned && ./build/bench_batch_perlin_reuse_aligned benchmarks/evaluations/perf-perlin-reuse-aligned__bench-cpp.json`
- 결과물: [`perf-perlin-reuse-aligned__bench-cpp.json`](../evaluations/perf-perlin-reuse-aligned__bench-cpp.json)
- 정확성 게이트: candidate reseed == baseline 비트 동일 — **PASS**. 병렬=순차, 두 모드 각각 — **PASS**.
- `sizeof(EnvSlot) == 128`, `alignof == 64` — baseline/candidate 모두 동일 타입이라 이번엔 크기/정렬 차이라는 교란 변수가 없다. (EXP-005의 `EnvSlotAligned`가 64바이트였던 것과 다른 이유: 여기선 `PerlinNoise` 멤버가 항상 포함돼 있어서 raw 크기가 커지고, `alignas(64)`가 다음 64의 배수인 128로 패딩했다.)

## 결과

| thread_count | resets/sec baseline | resets/sec candidate | steps/sec baseline | steps/sec candidate |
|---:|---:|---:|---:|---:|
| 1 | 157.7 ± 29.3 | 159.2 ± 31.2 | 23,493,507 ± 427,392 | 21,902,453 ± 3,663,162 |
| 2 | 317.8 ± 51.0 | 257.9 ± 57.2 | 44,294,903 ± 6,744,179 | 46,914,699 ± 4,250,131 |
| 4 | 391.6 ± 26.2 | 564.5 ± 62.6 | 53,163,828 ± 7,591,293 | 68,634,027 ± 13,250,353 |
| 6 | 556.3 ± 87.7 | 616.1 ± 32.9 | 75,651,233 ± 17,234,219 | 94,090,307 ± 17,304,505 |
| 8 | 526.2 ± 13.7 | 580.0 ± 94.0 | 73,810,581 ± 7,381,676 | 76,503,221 ± 10,355,793 |
| 12 | 651.7 ± 74.0 | 685.2 ± 54.4 | 91,516,548 ± 15,824,107 | 83,453,707 ± 15,982,977 |
| 16 | 601.7 ± 202.5 | 646.7 ± 56.7 | 82,093,285 ± 29,671,764 | 92,725,125 ± 34,130,967 |

thread_count=8: resets/sec 개선 **+10.2%**, steps/sec 개선 **+3.6%** — 둘 다 방향은 긍정적이지만 Accepted 기준(+20%)에 못 미치고, 표준편차 규모(예: baseline resets/sec sd=13.7이지만 candidate sd=94.0, 평균의 16%)를 감안하면 이 정도 차이는 노이즈 범위 안에 있다고 봐야 한다. 4~16 thread 전 구간에서 candidate가 baseline과 비슷하거나 약간 나은 정도이고, 어느 thread_count에서도 뚜렷한 우위는 없다.

## 결론

`Inconclusive` (지표별로 갈림 — `docs/evaluation-protocol.md` §18 기준 재판정)

라운드 넘버 기준(+20%/±10%) 대신 §18의 통계적 기준(`t = Δ/SE_diff`, `n=7`)으로 thread_count=8을 다시 판정하면:

- **resets/sec**: `t=1.50`, `pct=+10.2%` — `1.0<=|t|<2.5` 구간이라 **Inconclusive**(방향은 긍정적이지만 노이즈와 확실히 구별되지 않음).
- **steps/sec**: `t=0.56`, `pct=+3.6%` — `|t|<1.0`이라 **Rejected(효과 없음)**.

레이아웃을 통제하고 나니(EXP-004의 원래 교란 변수 제거) `PerlinNoise` 할당을 없애는 효과는 steps/sec에서는 확실히 없고(효과 없음), resets/sec에서는 있을 수도 있지만 이번 반복 횟수(7회)로는 노이즈와 구별하기에 부족하다. 종합하면: **512KB 힙 할당이 이 워크로드의 주된 병목이라는 증거는 여전히 없다** — EXP-004의 원래 결론(할당 경합 가설 기각)이 레이아웃을 통제한 뒤에도 유지된다. EXP-004가 크게 악화됐던 건 할당 자체가 아니라 그때 같이 바뀐 구조체 레이아웃(false sharing, EXP-005에서 resets/sec에 한해 통계적으로 확인됨) 때문이었다는 설명이 더 그럴듯하다.

## 다음 질문

`perf` 실험 계열(EXP-003~006)에서 지금까지 확인된 것을 정리하면: (1) 스레드 생성 오버헤드(해결됨, 영속 풀), (2) 512KB 힙 할당 경합(기각, 이번 실험), (3) `EnvSlot` 캐시 라인 정렬(reset phase에서 부분 효과 확인, step phase는 무반응). 아직 설명 안 된 건 step phase가 스레드 수에 따라 완전히 선형적으로 늘지 않는 이유([`perf-envslot-cache-align`](perf-envslot-cache-align.md)의 다음 질문과 동일) — `thermalErode`의 메모리 접근 패턴이나 하드웨어 자체의 병렬성 천장 쪽으로 조사 방향을 옮기는 게 다음 단계다. 이 세 번의 실험(EXP-004/005/006)으로 미시적 자료구조 튜닝의 수확체감을 확인했으므로, 다음은 더 큰 단위(예: `thermalErode` 자체를 배제한 fbm-only 스윕, 또는 이 정도가 이 하드웨어의 현실적 한계라는 결론)로 넘어갈 시점이다.
