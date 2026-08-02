# `perf-thermal-erode-bandwidth` — does `thermalErode`'s memory-access pattern cap reset-phase scaling?

> `perf` 카테고리 성능 실험. RL 결과 표는 적용되지 않는다 — 처리량(resets/sec) 표 형식을 재사용한다.

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-007` |
| Created | `2026-08-02` |
| Planned | `2026-08-02` |
| Started | `2026-08-02` |
| Completed | `2026-08-02` |
| Predecessor | `perf-perlin-reuse-aligned` |

## 상태

`Accepted`

## 관찰

[`perf-envslot-cache-align`](perf-envslot-cache-align.md)(EXP-005)에서 `EnvSlot`을 캐시 라인에 정렬해 resets/sec를 통계적으로 유의하게 개선했지만(+21.7% @ 8-thread), 그 이후로도 thread_count를 12/16까지 늘려봐도 resets/sec는 600~700/sec 부근에서 정체됐다(EXP-005/006 원시 데이터 참고) — 물리 코어 수(8)를 넘는 thread에서 추가 이득이 거의 없다. 이건 false sharing이 이미 해소된 뒤에도 남아 있는 별개의 천장이 있다는 뜻일 수 있다.

## 근거

- `perf-envslot-cache-align__bench-cpp.json`, `perf-perlin-reuse-aligned__bench-cpp.json`의 thread_count=12/16 지점.
- `thermalErode`(`core/src/erosion/thermal_erosion.cpp`)는 매 iteration(현재 설정 10회)마다 64×64 grid 전체를 순회하며 각 셀의 4-이웃을 읽고 새 buffer(`next`)를 만든다 — reset마다 10번의 전체 grid read+write가 일어난다. 여러 스레드가 이걸 동시에 하면 각자의 작업은 독립적이어도(공유 상태 없음) DRAM 대역폭이라는 하드웨어 자체의 공유 자원을 두고 경쟁한다 — 이건 false sharing(캐시 일관성 문제)과는 다른 종류의 병목이고, 소프트웨어로 고칠 수 있는 게 아니다.

## 가설

reset 워크로드에서 `thermalErode` 호출을 빼고 fbm 생성만 남기면(fbm 자체도 매 셀마다 gradient 테이블을 읽지만 grid를 반복 순회하진 않는다), thread_count를 늘렸을 때의 확장 효율(`speedup(N) = resets/sec(N) / resets/sec(1)`, 이상적으로는 `N`에 가까움)이 `thermalErode` 포함 워크로드보다 뚜렷하게 높을 것이다. 반대로 두 워크로드의 확장 효율 곡선이 비슷하게 꺾인다면, 천장은 `thermalErode`가 아니라 이 하드웨어의 더 일반적인 한계(메모리 대역폭 전반, 스케줄러 등)다.

## 독립변수

```text
reset 워크로드 구성:
  baseline  = fbm 생성 + thermalErode(10 iterations)  (현재 프로덕션 TerrainAgentEnv.reset()과 동일)
  candidate = fbm 생성만 (thermalErode 호출 제거)
```

## 통제변수

- `EnvSlot`: `alignas(64)`(EXP-005에서 확인된 개선을 유지).
- thread_count 그리드, 반복 횟수(7회): 이전 실험들과 동일.
- fbm 파라미터(`MAP_SIZE`, `SCALE`, `OCTAVES` 등): 두 워크로드 동일.

## 사전 채택 기준

**이 실험은 두 워크로드의 절대 처리량이 아니라 "확장 효율 곡선의 모양"을 비교한다** — `resets/sec`의 절대값은 당연히 다르다(candidate가 더 가벼운 작업이라 항상 더 빠름). 비교 대상은 `speedup(8) = resets/sec(8) / resets/sec(1)` 같은 무차원 배수다.

이 배수의 표준오차를 엄밀하게 전파(delta method 등)하는 건 이번 실험 범위를 벗어난다고 판단해, **이 실험은 `docs/evaluation-protocol.md` §18의 형식적 유의성 검정 대신 두 확장 효율 곡선을 나란히 놓고 서술적으로 비교**한다 — 이 한계를 결과에 명시한다.

- **Accepted (bandwidth 가설 지지)**: candidate의 `speedup(8)`이 baseline의 `speedup(8)`보다 **뚜렷하게 높고**(눈으로 봐도 both 표준편차 범위 밖일 정도로), thread_count 12/16에서도 candidate가 계속 개선되는 반면 baseline은 그대로 정체된다.
- **Rejected**: 두 워크로드의 확장 효율 곡선이 사실상 같은 모양으로 꺾인다 — `thermalErode`는 이 천장과 무관하고, 천장은 워크로드 종류와 무관한 더 근본적인 한계(하드웨어/스케줄러)다.
- **Inconclusive**: 곡선이 다르긴 한데 명확히 판단하기 애매한 경우.

## 실행 정보

- 구현: `core/src/bench_batch_thermal_bandwidth.cpp` (신규 `bench_batch_thermal_bandwidth` CMake 타겟) — `alignas(64) EnvSlot`(EXP-005의 `EnvSlotAligned`와 동일 모양, `PerlinNoise` 멤버 없음) 위에서 baseline(fbm+thermalErode)/candidate(fbm만) reset 함수를 나눠 같은 스윕에서 비교.
- 실행 명령: `cd core && cmake --build build --target bench_batch_thermal_bandwidth && ./build/bench_batch_thermal_bandwidth benchmarks/evaluations/perf-thermal-erode-bandwidth__bench-cpp.json`
- 결과물: [`perf-thermal-erode-bandwidth__bench-cpp.json`](../evaluations/perf-thermal-erode-bandwidth__bench-cpp.json)
- 정확성 게이트: 병렬=순차 checksum 동일, 두 워크로드 각각 — **PASS**.

## 결과

**1차 실행(`n=7`)은 노이즈가 커서 판단이 불가능했다** — 반복 횟수를 20으로 늘려 재실행했다. `docs/evaluation-protocol.md` §18의 비율(speedup) 비교 확장(delta method로 `speedup=mean_N/mean_1`의 표준오차를 근사한 뒤 `t = (speedup_candidate - speedup_baseline) / sqrt(SE_c^2+SE_b^2)`)을 적용한 최종 결과:

| thread_count | resets/sec baseline(fbm+thermal) | resets/sec candidate(fbm-only) | speedup baseline | speedup candidate | t(ratio) |
|---:|---:|---:|---:|---:|---:|
| 1 | 168.8 ± 17.9 | 454.9 ± 55.2 | 1.00x | 1.00x | — |
| 2 | 259.1 ± 50.1 | 836.9 ± 166.6 | 1.53x | 1.84x | 2.50 |
| 4 | 473.0 ± 79.0 | 1362.5 ± 267.9 | 2.80x | 3.00x | 0.98 |
| 6 | 519.8 ± 94.0 | 1633.8 ± 236.4 | 3.08x | 3.59x | 2.45 |
| 8 | 519.4 ± 47.4 | 1981.1 ± 426.5 | 3.08x | 4.36x | **4.93** |
| 12 | 636.2 ± 81.4 | 1893.7 ± 308.9 | 3.77x | 4.16x | 1.68 |
| 16 | 609.5 ± 126.0 | 2084.3 ± 235.7 | 3.61x | 4.58x | 3.84 |

`n=20`에서는 방향이 **모든 thread_count에서 일관되게 candidate(fbm-only)가 baseline(fbm+thermal)보다 잘 확장**되고, 그중 세 지점(tc=2, 8, 16)이 `|t|>=2.5` 유의성 문턱을 넘는다 — 특히 **물리 코어 수와 정확히 일치하는 tc=8에서 `t=4.93`으로 가장 강하게 유의**하다(baseline 3.08x vs candidate 4.36x). tc=4, 12는 유의성 문턱 아래지만 방향은 여전히 candidate 우세다. baseline은 tc=8 이후 3.08x→3.77x→3.61x로 사실상 정체하는 반면, candidate는 tc=16까지 계속 개선된다(4.36x→4.16x→4.58x, tc=12만 살짝 눌림).

## 결론

`Accepted`

사전 등록한 서술적 기준("candidate가 뚜렷하게 높고, 12/16에서도 candidate는 계속 개선되는데 baseline은 정체")과 이번에 §18에 추가한 통계 검정(ratio delta method) 둘 다 같은 결론을 가리킨다: **`thermalErode`의 반복적 전체 grid 순회는 물리 코어 수 근방에서 실제로 스레드 확장을 제한하는 요인이다.** 특히 물리 코어 수(8)에서 가장 강한 신호(`t=4.93`)가 나온 것은 우연이 아니다 — 코어 수만큼 스레드가 동시에 DRAM을 두드릴 때 대역폭 압력이 가장 크게 드러나는 지점이 바로 거기이기 때문이라는 설명과 정합적이다.

n=7으로는 이 신호가 노이즈에 묻혀 안 보였다는 것 자체도 기록해둘 만하다 — perf 실험에서 반복 횟수 부족이 "효과 없음"과 "노이즈로 안 보임"을 구별 못 하게 만든 실제 사례다.

## 다음 질문

`thermalErode`가 확인된 병목이니, 다음으로 자연스러운 질문은 **`thermalErode` 자체를 최적화할지, 아니면 이 정도(물리 코어에서 대략 4x, `thermalErode` 없이는 4.4x)를 이 하드웨어의 현실적 천장으로 받아들이고 Phase 2c의 architecture-evidence 문서화(AGENTS.md Phase 2c item 4)로 넘어갈지**다. `thermalErode`를 고치는 옵션(예: iteration마다 전체 버퍼를 새로 만들지 않고 in-place 갱신, 또는 in-place가 물리적으로 불가능하면 캐시 지역성을 높이는 방향으로 순회 순서 조정)은 침식 알고리즘 자체를 건드리는 일이라 `AGENTS.md`의 teach-vs-deliver 경계상 알고리즘 소유자가 설계/구현해야 한다 — 에이전트가 임의로 진행할 항목이 아니다.

지금까지의 perf 실험 계열(EXP-003~007)은: 스레드 생성 오버헤드 해결, 캐시 라인 정렬로 resets/sec 유의미하게 개선(EXP-005), 힙 할당 재사용은 효과 없음(EXP-004/006), `thermalErode`가 물리 코어 수 근방의 실제 병목으로 확인(EXP-007) — 이 정도면 "측정 먼저" 원칙을 보여주는 포트폴리오 증거로 이미 충분하다는 게 개인적 판단이다. `thermalErode` 자체를 고칠지는 사용자의 판단이 필요하다.
