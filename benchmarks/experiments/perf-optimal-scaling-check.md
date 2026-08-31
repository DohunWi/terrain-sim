# `perf-optimal-scaling-check` — does the combined best configuration clear EXP-003's original 4x target?

> `perf` 카테고리 성능 실험. baseline/candidate 비교가 아니라 **지금까지 채택된 개선을 모두 합친 하나의 설정**을 EXP-003이 원래 세운 절대 목표(물리 코어 8개에서 4배 이상)에 대해 재확인하는 확인용 실험이다 — 새 독립변수는 없다.

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-009` |
| Created | `2026-08-02` |
| Planned | `2026-08-02` |
| Started | `2026-08-02` |
| Completed | `2026-08-02` |
| Predecessor | `perf-thermal-erode-alloc` |

## 상태

`Accepted (resets/sec)` / `Inconclusive (steps/sec)` — 지표별로 다른 결론(§18 원칙). steps/sec는 원래 `Rejected-as-hardware-ceiling`으로 기록했으나 2026-08-31에 정정 (아래 「정정」 절)

## 관찰

perf 실험 계열(EXP-003~008)에서 채택된(Accepted) 변경은 두 가지다: `EnvSlot`의 캐시 라인 정렬(EXP-005, resets/sec +21.7%)과 `thermalErode`의 힙 할당 제거(EXP-008, resets/sec +72.9%~+196.3%). `PerlinNoise` 테이블 재사용(EXP-004/006)은 기각/불확정이라 채택하지 않는다. `thermalErode` 리팩토링은 이미 `core/src/erosion/thermal_erosion.cpp`의 프로덕션 코드에 반영됐고, `EnvSlot` 정렬은 `core/src/bench_batch_cache_align.cpp`(EXP-005 하네스)에 이미 구현돼 있다 — 이 둘을 합친 설정은 새로 만들 필요 없이, **`bench_batch_cache_align`을 다시 빌드해서 재실행하기만 하면 된다**(그 바이너리가 링크하는 `thermal_erosion.cpp`가 이제 EXP-008의 빠른 버전이므로).

## 근거

- `perf-envslot-cache-align__bench-cpp.json`(EXP-005, `thermalErode` 리팩토링 이전 수치)
- `perf-thermal-erode-alloc__bench-cpp.json`(EXP-008)
- `perf-parallel-envs.md`(EXP-003)의 원래 사전 채택 기준: 물리 코어 수(8)에서 steps/sec, resets/sec 배수가 각각 4배 이상.

## 가설

`EnvSlot` 정렬(이미 반영됨) + `thermalErode` 할당 제거(이미 반영됨)를 합친 현재 상태로 EXP-005의 하네스를 재실행하면, resets/sec 배수가 물리 코어 수(8)에서 4배를 넘길 것이다. steps/sec는 `thermalErode`와 무관한 경로(순수 `stepRigidBody` 반복)라 이 리팩토링의 영향을 받지 않으므로, EXP-005/007에서 관찰된 수준(3~5배대)에서 크게 안 벗어날 것으로 예상한다.

## 독립변수

없음 — 이건 새 변수를 바꾸는 실험이 아니라, 이미 채택된 변경들을 합친 현재 상태를 절대적 목표치에 대해 재측정하는 확인 실험이다.

## 통제변수

- `core/src/bench_batch_cache_align.cpp`(EXP-005 하네스)를 코드 변경 없이 재빌드/재실행 — `EnvSlotAligned`(정렬됨, `PerlinNoise` 멤버 없음)와 `EnvSlotDefault`(정렬 없음) 비교는 그대로 유지하되, 관심 대상은 `EnvSlotAligned`의 절대 배수다.
- thread_count 그리드 `{1,2,4,6,8,12,16}`, 반복 횟수 20(EXP-007/008 이후 기본값으로 승격).

## 사전 채택 기준

- **Accepted**: `EnvSlotAligned`의 thread_count=8에서 resets/sec 배수(자기 자신의 1-thread 대비) **및** steps/sec 배수가 **모두 4배 이상**.
- **Rejected**: 둘 중 하나라도 2배 미만.
- **Inconclusive**: 그 사이.

## 실행 정보

- 실행 명령: `cd core && cmake --build build --target bench_batch_cache_align && ./build/bench_batch_cache_align benchmarks/evaluations/perf-optimal-scaling-check__bench-cpp.json`
- 결과물: `benchmarks/evaluations/perf-optimal-scaling-check__bench-cpp.json`

## 결과

`bench_batch_cache_align`을 재빌드(현재의 빠른 `thermalErode` 링크)해서 `n=20`으로 재실행:

| thread_count | resets/s default | resets/s aligned | steps/s default | steps/s aligned |
|---:|---:|---:|---:|---:|
| 1 | 294.5 ± 9.6 | 292.5 ± 12.2 | 23,848,271 ± 993,565 | 22,919,189 ± 2,237,617 |
| 2 | 583.6 ± 18.7 | 591.2 ± 16.0 | 46,525,031 ± 2,323,892 | 47,766,929 ± 1,270,509 |
| 4 | 915.0 ± 120.8 | 965.9 ± 125.5 | 69,382,320 ± 14,881,569 | 65,095,071 ± 12,310,876 |
| 6 | 1114.3 ± 173.4 | 1078.3 ± 261.6 | 68,552,718 ± 10,166,300 | 71,286,536 ± 15,874,953 |
| 8 | 1335.0 ± 269.9 | 1570.4 ± 204.9 | 76,951,111 ± 8,927,769 | 81,987,300 ± 9,152,503 |
| 12 | 1341.2 ± 142.2 | 1414.4 ± 181.5 | 96,001,472 ± 15,111,932 | 109,401,408 ± 10,729,890 |
| 16 | 1419.7 ± 214.5 | 1382.3 ± 168.9 | 101,910,145 ± 14,452,280 | 105,657,406 ± 17,430,729 |

thread_count=8에서 `EnvSlotAligned`의 자기 자신(1-thread) 대비 배수: **resets/sec 5.33x**, **steps/sec 3.44x**.

`EXP-003`의 원래 baseline(`thermalErode` 리팩토링 이전, `perf-parallel-envs__bench-cpp.json`)과 비교하면 resets/sec는 177.6/sec(1-thread)→523.2/sec(8-thread, 2.95x)이던 것이 이제 292.5/sec(1-thread)→1570.4/sec(8-thread, **5.33x**)로 완전히 다른 체급이 됐다.

## 결론

두 지표가 서로 다른 상태에 있다(§18: 지표별로 결론을 따로 기술).

- **resets/sec: `Accepted`.** EXP-003이 원래 세운 4배 목표(사전 등록 당시 못 넘겼던 바로 그 기준)를 이제 **5.33x**로 넘긴다. EXP-005(정렬)와 EXP-008(할당 제거)이 합쳐진 효과다 — reset-phase가 이 실험 계열 전체의 출발점이었던 병목이었는데, 이제 해소됐다고 볼 수 있다.
- **steps/sec: 여전히 4배 미만(3.44x)이지만, 이건 이번 리팩토링과 무관한 별개의 한계로 보인다.** `stepRigidBody`는 `thermalErode`를 전혀 호출하지 않으므로 이번 개선의 영향을 받을 이유가 없고, 실제로 EXP-003(3.14x)·EXP-005(3.19x)·EXP-006(3.39x)·이번(3.44x)까지 이 프로젝트의 모든 perf 실험에서 steps/sec 배수는 일관되게 3~4배대에 머물렀다. 스레드 풀 재사용, 캐시 정렬, 할당 제거 중 어느 것도 이 숫자를 움직이지 못했다 — **소프트웨어로 고칠 수 있는 병목이 아니라 이 8코어(P/E 비대칭) 개발 머신에서 독립적인 순수 계산 워크로드가 갖는 현실적인 병렬성 천장으로 받아들이는 게 타당하다.**

## 정정 (2026-08-31)

위 결론의 steps/sec 항목에서 "이 8코어(P/E 비대칭) 개발 머신의 병렬성 천장"이라고 한 원인 귀속을 철회한다. 근거는 이 문서의 결과 표 자체다. `EnvSlotAligned` steps/sec는 thread_count=8에서 81.99M이지만 thread_count=12에서 109.40M(1-thread 22.92M 대비 **4.77x**), 16에서 105.66M(4.61x)로 더 올라간다. 병렬성 상한이 ~3.4x라면 오버서브스크립션으로 그것을 넘길 수 없다. (참고: 위 「결과」의 5.33x·3.44x는 "자기 자신(1-thread) 대비"라고 썼지만 실제로는 default 슬롯 1-thread(294.5/s, 23.85M/s)를 분모로 둔 값이다. aligned 1-thread 기준이면 5.37x·3.58x.)

관찰 자체 — 8스레드에서 ~3.4x이고 EXP-003~009 내내 어떤 개입에도 움직이지 않았다 — 는 유효하다. 원인은 미확정이며, 현재 가장 그럴듯한 가설은 하네스 구조다. `WorkerPool`은 스레드당 env 1개를 고정 배정하고 `std::barrier`로 phase를 동기화하므로 phase 시간은 가장 느린 스레드가 결정하고, 4P+4E 코어에서 8스레드를 돌리면 E-core에 앉은 스레드가 꼬리가 된다. 12·16스레드에서 오르는 것은 OS 스케줄링이 그 불균형을 시간분할로 완화하기 때문으로 설명된다. 검증 실험: env 수를 스레드 수보다 크게 두고(예: 32 env / 8 thread) 동적으로 분배해 8스레드 배수가 4x를 넘는지 확인한다. 이 실험은 아직 수행하지 않았다.

## 다음 질문

reset-phase 문제(이 실험 계열의 원래 동기)는 해소됐다고 결론짓는다. steps/sec의 ~3.5x 천장은 이 하드웨어의 한계로 받아들이고, 추가 소프트웨어 최적화를 더 찾기보다 여기서 perf 미시 실험 계열(EXP-003~009)을 마무리하는 것을 권한다. 다음 단계는 Phase 2c의 architecture-evidence 문서화(AGENTS.md item 4) — 이번 실험 계열 전체(스레드 풀 → 정렬 → 할당 경합 기각 → 알고리즘 병목 확인 → 최종 확인)를 before/after 성능표와 함께 공개 문서(`docs/`)에 정리하는 것.
