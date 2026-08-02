# `perf-envslot-cache-align` — Cache-line-padded `EnvSlot` vs default layout

> `perf` 카테고리 성능 실험. RL 결과 표는 적용되지 않는다 — [`perf-parallel-envs`](perf-parallel-envs.md)/[`perf-perlin-table-reuse`](perf-perlin-table-reuse.md)와 같은 처리량(steps/sec, resets/sec) 표 형식을 재사용한다.

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-005` |
| Created | `2026-08-02` |
| Planned | `2026-08-02` |
| Started | `2026-08-02` |
| Completed | `2026-08-02` |
| Predecessor | `perf-perlin-table-reuse` |

## 상태

`Inconclusive`

## 관찰

[`perf-perlin-table-reuse`](perf-perlin-table-reuse.md)에서 `EnvSlot`에 상주 `PerlinNoise` 멤버를 추가했더니(할당 자체는 사라졌는데도) thread_count=8에서 resets/sec -24%, steps/sec -15% — baseline보다 **더 나빠졌다**. 힙 할당자 경합 가설은 기각됐고, 대신 `EnvSlot` 구조체 크기/정렬이 바뀌면서 `std::vector<EnvSlot>`(연속 메모리) 안에서 인접 슬롯들이 캐시 라인을 공유하게 됐을 가능성(false sharing)이 다음 질문으로 등록됐다. 이건 애초에 [`perf-parallel-envs`](perf-parallel-envs.md)(EXP-003)에서부터 있던 문제일 수 있다 — 그 실험도 이미 8-thread에서 4배 문턱을 못 넘겼고(steps/sec 3.14x, resets/sec 2.95x) 6→8 thread 구간에서 resets/sec가 비단조적으로 하락(555.6→523.2)했다.

## 근거

- `perf-parallel-envs__bench-cpp.json`, `perf-perlin-table-reuse__bench-cpp.json`의 원시 수치.
- `EnvSlot`이 `std::vector<EnvSlot> slots_`(연속 메모리)로 저장되고, 각 워커 스레드가 자신의 인덱스 `i`에 해당하는 슬롯만 쓰지만 인접 인덱스 슬롯과 물리적으로 가까운 메모리 주소를 가진다 — 구조체 크기가 캐시 라인(보통 64바이트) 경계와 안 맞으면 슬롯 경계가 캐시 라인 중간에서 걸릴 수 있다.

## 가설

`EnvSlot`을 캐시 라인 경계(64바이트)에 맞춰 정렬/패딩하면(`alignas(64)`), 슬롯 간 false sharing이 제거되어 [`perf-parallel-envs`](perf-parallel-envs.md)에서 관찰된 준선형 이하 스케일링(8-thread 3.14x/2.95x)이 개선되고, 6→8 thread 구간의 비단조적 하락도 사라진다.

## 독립변수

```text
EnvSlot 메모리 레이아웃:
  baseline  = 현재 EnvSlot (정렬 지정 없음, perf-parallel-envs와 동일 구조)
  candidate = EnvSlot을 alignas(64)로 캐시 라인 경계에 정렬
```

`PerlinNoise` 할당 정책은 `perf-perlin-table-reuse`에서 Rejected로 결론 났으므로, 이번 실험은 그 변경을 되돌리고(매 reset마다 새 `PerlinNoise` 생성, EXP-003과 동일한 baseline 정책) **정렬만** 단일 변수로 바꾼다.

## 통제변수

- thread_count 그리드, 반복 횟수(7회), heightmap/terrain/physics 상수: `perf-parallel-envs`/`perf-perlin-table-reuse`와 전부 동일.
- `PerlinNoise` 할당 정책: 매 reset마다 새로 생성(`perf-perlin-table-reuse`의 candidate가 아니라 그 실험의 baseline과 동일 정책으로 고정).
- 워커 풀 동기화 방식(`std::barrier` 영속 풀), step phase 규모(에피소드 50개분): 이전 실험들과 동일.

## 사전 채택 기준

### 정확성 (선행 조건)

- 정렬 변경은 값에 영향을 주면 안 된다 — 병렬=순차 비트 동일성 gate가 여전히 통과해야 한다.

### 처리량

이번 실험 자체의 thread_count=1 대비 배수(내부 speedup)와, `perf-parallel-envs`(EXP-003)의 원래 8-thread 수치 대비 직접 비교 두 가지를 본다.

- **Accepted**: thread_count=8에서 candidate의 steps/sec, resets/sec 배수(자체 1-thread 대비)가 **모두 4배 이상**(EXP-003이 원래 넘지 못했던 문턱)이고, `perf-parallel-envs`의 원래 8-thread 절대값 대비 **+30% 이상** 개선하며, thread_count 6→8 구간의 비단조적 하락이 사라진다(단조 비감소).
- **Rejected**: `perf-parallel-envs` 원래 수치 대비 개선폭이 +15% 미만 — false sharing 가설 기각, 다른 원인(메모리 대역폭 자체 한계, P/E 코어 스케줄링, thermalErode의 순차적 데이터 의존성 등) 조사 필요.
- **Inconclusive**: 그 사이, 또는 6→8 비단조성은 사라졌지만 4배 문턱은 못 넘기는 등 지표가 엇갈리는 경우.

## 실행 정보

- 구현: `core/src/bench_batch_cache_align.cpp` (신규 `bench_batch_cache_align` CMake 타겟) — `perf-parallel-envs`의 `bench_batch.cpp`와 같은 `std::barrier` 영속 워커 풀 구조를 템플릿화해서, `EnvSlotDefault`(정렬 지정 없음)와 `EnvSlotAligned`(`alignas(64)`) 두 타입에 대해 동일한 스윕을 실행.
- 실행 명령: `cd core && cmake --build build --target bench_batch_cache_align && ./build/bench_batch_cache_align benchmarks/evaluations/perf-envslot-cache-align__bench-cpp.json`
- 결과물: [`perf-envslot-cache-align__bench-cpp.json`](../evaluations/perf-envslot-cache-align__bench-cpp.json)
- 정확성 게이트: 병렬=순차 비트 동일성, 두 레이아웃 각각 — **PASS**.
- **레이아웃 확인**: `sizeof(EnvSlotDefault) == 64`, `alignof == 8` / `sizeof(EnvSlotAligned) == 64`, `alignof == 64`. 즉 `Heightmap`+`RigidBody` 조합이 이미 정확히 64바이트라 `alignas(64)`가 슬롯 간 간격을 바꾸지는 않았다 — 바뀐 건 오직 **`std::vector`가 반환하는 버퍼의 시작 주소가 64바이트 경계에 강제로 맞춰지는지 여부**뿐이다. `EnvSlotDefault`는 시작 주소가 임의 오프셋을 가질 수 있어서, 슬롯 크기가 64바이트와 정확히 같아도 모든 슬롯이 캐시 라인 경계에서 항상 같은 만큼 어긋나 있을 수 있다(전형적인 false-sharing 세팅).

## 결과

| thread_count | resets/sec default(unaligned) | resets/sec aligned | steps/sec default(unaligned) | steps/sec aligned |
|---:|---:|---:|---:|---:|
| 1 | 180.1 ± 1.7 | 161.8 ± 27.9 | 23,745,258 ± 350,908 | 20,302,393 ± 5,563,916 |
| 2 | 244.5 ± 46.4 | 254.6 ± 52.8 | 40,918,702 ± 9,426,784 | 40,687,551 ± 9,617,068 |
| 4 | 434.4 ± 53.3 | 439.3 ± 75.5 | 56,027,471 ± 11,329,426 | 55,997,220 ± 16,956,213 |
| 6 | 520.8 ± 97.5 | 563.1 ± 34.0 | 55,292,135 ± 10,974,797 | 71,654,787 ± 11,969,808 |
| 8 | 585.6 ± 89.2 | 712.9 ± 37.5 | 75,699,057 ± 12,994,241 | 75,772,830 ± 7,860,747 |
| 12 | 636.1 ± 70.8 | 681.2 ± 98.3 | 81,523,982 ± 17,912,104 | 77,316,861 ± 15,849,129 |
| 16 | 622.4 ± 18.2 | 634.4 ± 80.4 | 96,565,602 ± 16,257,789 | 86,407,607 ± 10,116,595 |

thread_count=8: resets/sec 개선 **+21.7%**(585.6 → 712.9), steps/sec 개선 **+0.1%**(사실상 무변화). aligned 자체의 8-thread 배수는 자기 1-thread 대비 resets 3.96x / steps 3.19x — 사전 등록한 4x 문턱에 resets는 거의 닿았지만(3.96x) steps는 못 미쳤다.

두 지표가 **다른 방향**을 가리킨다: resets/sec는 뚜렷이 개선됐고(6→8 구간 비단조 하락도 aligned에서는 안 보인다: 563.1→712.9로 단조 증가), steps/sec는 정렬과 거의 무관했다. 이건 그 자체로 설명이 된다 — reset phase는 매번 새 `Heightmap`/`PerlinNoise` 힙 할당과 `EnvSlot`의 작은 헤더 필드(`RigidBody`, 포인터들) 초기화가 몰려서 일어나는 구간이라 슬롯 간 캐시 라인 경계가 중요하고, step phase는 각 스레드가 자기 슬롯 안의 같은 데이터(자기 `body`/`terrain`)를 반복해서 읽고 쓰는 구간이라 인접 슬롯과의 캐시 라인 공유가 상대적으로 덜 중요하다.

## 결론

`Inconclusive` (지표별로 갈림 — `docs/evaluation-protocol.md` §18 기준 재판정)

사전 등록 당시 라운드 넘버 기준(+30%/+15%) 대신, §18의 통계적 기준(`t = Δ/SE_diff`, `n=7`)으로 thread_count=8을 다시 판정하면:

- **resets/sec**: `t=3.48`, `pct=+21.7%` — `|t|>=2.5`이고 개선 방향이며 `pct>=10%`이므로 **Accepted**. false sharing 가설이 reset phase에 대해서는 통계적으로 뒷받침된다.
- **steps/sec**: `t=0.01`, `pct=+0.1%` — `|t|<1.0`이라 **Rejected(효과 없음)**. 정렬은 step phase 처리량에 사실상 아무 영향이 없었다.

즉 이번 실험은 "Inconclusive 하나"가 아니라 **"resets/sec는 Accepted, steps/sec는 효과 없음(Rejected)"** 두 개의 분명한 결론이다. false-sharing 가설은 reset phase에 대해서는 통계적으로 지지되고, step phase에 대해서는 애초에 해당하지 않았다는 것이 명확해졌다 — step phase의 스케일링 한계는 다른 원인(아래 다음 질문)에서 찾아야 한다.

## 다음 질문

reset phase의 개선은 false-sharing 가설과 일치하지만, step phase가 그대로인 이유를 설명할 다른 병목이 남아 있다. 후보: (1) `thermalErode`가 10 iteration 동안 매번 전체 grid를 순회하며 `next` 버퍼를 만드는데, 이 반복적 메모리 접근 패턴 자체가 스레드 수와 무관하게 이미 메모리 대역폭에 근접해 있어서 스레드를 늘려도 기여분이 줄어드는 것일 수 있다 — 확인 방법: `thermalErode` 호출을 빼고 fbm 생성만으로 같은 스윕을 돌려 resets/sec 배수가 바뀌는지 본다. (2) `stepRigidBody` 자체는 각 스레드가 완전히 독립된 데이터만 건드리므로, step phase가 정렬에 반응하지 않는 건 이미 그 구간엔 false sharing이 거의 없었다는 뜻일 수 있고, 그렇다면 step phase의 스케일링 한계는 순수 CPU/메모리 대역폭 한계(하드웨어 자체의 병렬성 천장)일 가능성이 높다 — 이 경우 추가 최적화보다 "이 정도가 이 하드웨어의 현실적 천장"이라는 결론으로 이 실험 계열을 마무리하는 것도 고려 대상이다.

이미 계획된 후속 실험: `Accepted`로 끝나면, `EnvSlot`에 `PerlinNoise` 재사용 멤버를 다시 추가해([`perf-perlin-table-reuse`](perf-perlin-table-reuse.md), EXP-004의 candidate) 정렬된 `EnvSlot` 위에서 그 효과를 재검증한다 — EXP-004가 `Rejected`된 건 할당 재사용 자체가 나빴다기보다 구조체 크기 변경이 false sharing을 새로 만들어 효과가 가려졌을 가능성이 있기 때문이다. 이번 결과가 `Inconclusive`이긴 하지만 reset phase에서 실질적 개선이 있었으므로, 이 후속 실험은 여전히 해볼 가치가 있다.

**이미 계획된 후속 실험**: `Accepted`로 끝나면, `EnvSlot`에 `PerlinNoise` 재사용 멤버를 다시 추가해([`perf-perlin-table-reuse`](perf-perlin-table-reuse.md), EXP-004의 candidate) 정렬된 `EnvSlot` 위에서 그 효과를 재검증한다 — EXP-004가 `Rejected`된 건 할당 재사용 자체가 나빴다기보다 구조체 크기 변경이 false sharing을 새로 만들어 효과가 가려졌을 가능성이 있기 때문이다.
