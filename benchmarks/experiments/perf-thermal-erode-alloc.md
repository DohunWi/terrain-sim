# `perf-thermal-erode-alloc` — eliminate per-cell/per-iteration heap allocation in `thermalErode`

> `perf` 카테고리 성능 실험. RL 결과 표는 적용되지 않는다 — 처리량(resets/sec) 표 형식을 재사용한다.

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-008` |
| Created | `2026-08-02` |
| Planned | `2026-08-02` |
| Started | `2026-08-02` |
| Completed | `2026-08-02` |
| Predecessor | `perf-thermal-erode-bandwidth` |

## 상태

`Accepted`

## 관찰

[`perf-thermal-erode-bandwidth`](perf-thermal-erode-bandwidth.md)(EXP-007)에서 `thermalErode`가 물리 코어 수 근방의 실제 스레드 확장 병목임이 통계적으로 확인됐다(`t=4.93` @ 8-thread). 코드 리뷰 결과 두 가지 자원 수명(ownership) 문제가 확인됐다:

1. `findLowestNeighbor`(`core/src/erosion/thermal_erosion.cpp:4`)가 **셀 하나·iteration 한 번마다 `std::vector<LowestNeighbor>`를 새로 힙 할당**한다. 이 프로젝트의 실제 설정(`MAP_SIZE=64`, `EROSION_ITERATIONS=10`)으로는 reset 1회당 `64×64×10 = 40,960`번의 힙 할당이다 — EXP-004/006에서 테스트했던 `PerlinNoise`의 reset당 1회 512KB 할당보다 훨씬 잦은 패턴이라, 실제로는 이쪽이 더 큰 병목일 가능성이 있다.
2. `thermalErode`(같은 파일 28번째 줄)의 `Heightmap next = height;`가 **매 iteration마다 전체 grid를 새로 할당+복사**한다 — 10 iteration이면 reset마다 10번의 추가 `Heightmap` 힙 할당.

## 근거

- `perf-thermal-erode-bandwidth__bench-cpp.json`의 통계적으로 유의한 결과.
- `core/src/erosion/thermal_erosion.cpp` 코드 자체(4번째 줄 `std::vector<LowestNeighbor> candidates;`, 28번째 줄 `Heightmap next = height;`).

## 가설

`findLowestNeighbor`를 `std::array<LowestNeighbor,4>`+유효 개수로, `next` 버퍼를 매 iteration 재할당 대신 미리 만든 두 버퍼를 재사용(역할만 바꾸고 값은 계속 복사)하는 방식으로 바꾸면 — 힙 할당 자체가 사라지므로 (1) 순차 실행 시 `thermalErode` 자체의 wall-clock이 줄고 (2) [`perf-thermal-erode-bandwidth`](perf-thermal-erode-bandwidth.md)에서 확인된 스레드 확장 한계도 완화된다.

**이 실험의 가장 중요한 전제 조건은 성능이 아니라 정확성이다**: 이 리팩토링은 매 셀의 계산 순서(어떤 이웃을 어떤 순서로 봤는지, 합산 순서 등)를 바꾸지 않아야 한다 — 그래야 부동소수점 결과가 리팩토링 전후로 비트 단위 동일하다. `thermalErode`는 `training/env.py`의 `TerrainAgentEnv.reset()`이 실제로 호출하는 함수이자 RL 환경이 frozen 상태이므로, 결과가 조금이라도 달라지면 이건 "성능 개선"이 아니라 `docs/evaluation-protocol.md` §14 분류상 "physics tuning"이 되어 기존 평가 결과들과 섞어 쓸 수 없게 된다.

## 독립변수

```text
thermalErode 구현:
  baseline  = 현재 구현 (findLowestNeighbor가 std::vector 반환, next를 매 iteration 재할당)
  candidate = std::array<LowestNeighbor,4>+count, 재사용 가능한 두 버퍼 사이 값 복사(재할당 없음)
```

## 통제변수

- 계산 순서: 이웃 탐색 순서(`dx`/`dy` 배열 순서), 후보 순회 순서(합산/분배), 조건문(`>=`, `>`) — **정확히 동일하게 유지**한다. 이건 "무엇을 바꿔도 되는가"에 대한 통제변수이지, 튜닝 대상이 아니다.
- `EnvSlot`: `alignas(64)`(EXP-005/007 계승).
- thread_count 그리드, 반복 횟수: EXP-007에서 `n=7`이 노이즈에 신호를 묻어버린 전례가 있으므로 처음부터 `n=20`으로 시작한다.
- terrain/physics 상수: 이전 실험들과 동일(`TALUS_ANGLE=0.15`, `EROSION_RATE=0.3`, `EROSION_ITERATIONS=10`).

## 사전 채택 기준

### 정확성 (절대적 선행 조건 — 이게 실패하면 성능과 무관하게 `Rejected`)

- **여러 개(최소 20개) 서로 다른 seed의 heightmap에 대해, baseline과 candidate의 `thermalErode` 출력이 셀 단위로 완전히(비트 단위) 동일해야 한다.** 체크섬(합계) 비교가 아니라 `Heightmap::at(x,y)`를 모든 `(x,y)`에 대해 `==`로 비교한다 — 체크섬은 서로 다른 셀의 오차가 상쇄돼 숨을 수 있어서 이 gate로는 부족하다.
- 병렬=순차 비트 동일성(기존 gate와 동일한 방식)도 baseline/candidate 각각 확인한다.
- 정확성이 깨지면(비트가 하나라도 다르면), 그 차이가 FMA/컴파일러 부동소수점 축약 때문인지 실제 로직 변경 때문인지 규명한 뒤 — `-ffp-contract=off` 같은 플래그로 해결 가능하면 그렇게 하고, 해결 안 되면 이 실험은 `Rejected`로 종료하고 "값이 달라지는 최적화"는 별도의 physics-tuning 트랙(재학습/재평가 필요)으로 넘긴다.

### 처리량

정확성 gate를 통과한 뒤에만 의미가 있다. `docs/evaluation-protocol.md` §18의 직접 비교(비율이 아니라 절대 `resets/sec` 평균 비교, `t=Δ/SE_diff`)를 물리 코어 수(8)에서 적용한다.

- **Accepted**: `|t|>=2.5`이고 candidate가 더 빠르며 `pct_change>=10%`.
- **Rejected**: `|t|<1.0`, 또는 정확성 gate 실패.
- **Inconclusive**: 그 사이.

## 실행 정보

- 구현: `core/src/erosion/thermal_erosion.{h,cpp}`의 `findLowestNeighbor`/`thermalErode` 리팩토링(사용자 구현) — `findLowestNeighbor`가 `NeighborList{std::array<LowestNeighbor,4>, count}`를 반환하도록, `thermalErode`의 `next` 버퍼 선언을 반복문 밖으로 빼서 매 iteration엔 대입만 하도록. 부수적으로 `thermal_erosion.h`에 빠져 있던 `#include <array>`도 추가(그동안 다른 헤더의 전이적 include로 우연히 컴파일되던 것).
- 정확성 회귀 테스트: `core/tests/erosion_reference.{h,cpp}`(리팩토링 전 코드를 `thermalErodeRef`/`findLowestNeighborRef`로 이름만 바꿔 그대로 보존한 테스트 전용 사본) + `core/tests/erosion_test.cpp`(20개 서로 다른 seed에서 새 `thermalErode`와 `thermalErodeRef`의 출력을 **셀 단위**로 `ASSERT_EQ` 비교, `findLowestNeighbor`도 이웃 목록 자체를 셀별로 비교) — **PASS**, 20 seed 전부 비트 단위 동일.
- 벤치마크 하네스: `core/src/bench_batch_thermal_erode_alloc.cpp` (신규 `bench_batch_thermal_erode_alloc` CMake 타겟) — `alignas(64) EnvSlot` 위에서 baseline(`thermalErodeRef`)/candidate(`thermalErode`)를 같은 스윕으로 비교, `docs/evaluation-protocol.md` §18의 직접 비교(비율 아님) 검정 적용.
- 실행 명령: `cd core && cmake --build build --target bench_batch_thermal_erode_alloc && ./build/bench_batch_thermal_erode_alloc benchmarks/evaluations/perf-thermal-erode-alloc__bench-cpp.json`
- 결과물: [`perf-thermal-erode-alloc__bench-cpp.json`](../evaluations/perf-thermal-erode-alloc__bench-cpp.json)

## 결과

| thread_count | resets/sec baseline | resets/sec candidate | t | pct_change |
|---:|---:|---:|---:|---:|
| 1 | 170.5 ± 19.3 | 294.9 ± 8.9 | 26.16 | +72.9% |
| 2 | 216.7 ± 8.6 | 584.4 ± 19.2 | 78.02 | +169.6% |
| 4 | 405.2 ± 23.9 | 935.0 ± 171.2 | 13.71 | +130.8% |
| 6 | 456.5 ± 68.4 | 1070.3 ± 129.4 | 18.76 | +134.4% |
| 8 | 517.0 ± 90.3 | 1531.8 ± 282.1 | 15.32 | +196.3% |
| 12 | 510.0 ± 78.9 | 1233.1 ± 196.8 | 15.25 | +141.8% |
| 16 | 520.9 ± 64.0 | 1413.9 ± 158.3 | 23.39 | +171.4% |

모든 thread_count에서 `|t|`가 13 이상 — 사전 등록한 유의성 문턱(2.5)을 압도적으로 넘는다. 물리 코어 수(8)에서 `t=15.32`, `pct=+196.3%`(517→1532 resets/sec, 거의 3배). 다른 실험들(EXP-004~007)에서 본 어떤 개선보다 크고 명확하다.

## 결론

`Accepted`

`findLowestNeighbor`의 셀당 힙 할당(reset당 40,960회)과 `next` 버퍼의 iteration당 재할당을 제거한 것이, 지금까지의 perf 실험 계열(EXP-003~007)에서 시도한 어떤 최적화보다(정렬 +21.7%, 대역폭 분리 등) **압도적으로 큰 효과**를 냈다. 정확성 게이트(20 seed 셀 단위 비트 동일)도 통과했으므로, 이 변경은 RL 환경의 frozen 상태를 깨지 않는다 — `docs/evaluation-protocol.md` §14 분류상 "instrumentation"(동작이 안 바뀌는 변경)에 해당하며 기존 baseline/평가 결과를 그대로 유지할 수 있다.

## 다음 질문

이번 실험으로 명확해진 것: **이 프로젝트의 실제 성능 병목은 스레드 확장성이 아니라 애초에 알고리즘 코드에 있던 불필요한 힙 할당이었다.** EXP-003~007이 스레드 풀/정렬/대역폭 쪽을 붙잡고 씨름하는 동안, 셀당 40,960번의 `std::vector` 할당이 훨씬 더 큰 병목으로 남아 있었다는 뜻이다. 다음으로 확인해볼 만한 것:

1. 이 수정이 [`perf-parallel-envs`](perf-parallel-envs.md)(EXP-003)의 원래 목표(물리 코어 8개에서 steps/sec·resets/sec 4배 이상)를 이제는 넘기는지 재확인 — `thermalErode`가 병목이 아니게 된 상태에서 EXP-003의 원래 스윕을 다시 돌려본다.
2. `droplet_erosion.cpp`에도 비슷한 패턴(셀/물방울 단위 컨테이너 할당)이 있는지 점검 — 같은 종류의 문제가 반복되고 있을 가능성.
3. 이 정도 개선이면 Phase 2c의 architecture-evidence 문서화(AGENTS.md item 4)에 넣을 "before/after 성능표"의 핵심 증거로 충분해 보인다 — 이제 이쪽으로 넘어가는 것을 고려할 시점이다.
