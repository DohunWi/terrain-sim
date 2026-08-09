# `perf-heightmap-at-inline` — `Heightmap::at()` 인라인 전후 재측정

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-010` |
| Created | `2026-08-08` |
| Planned | `N/A (retrospective confirmation)` |
| Started | `2026-08-08` |
| Completed | `2026-08-08` |
| Predecessor | `perf-optimal-scaling-check` |

## 상태

`Accepted` — 단, 구현 이후 실시한 사후 확인 측정이며 사전등록 실험은 아니다.

## 관찰

`thermalErode()`의 scatter/pull 구조를 조사하던 중, 이웃 접근 방식보다 다른 번역 단위에 있던 `Heightmap::at()` 호출 경계가 컴파일러 최적화를 제한한다는 증거를 얻었다.

## 가설과 독립변수

- 가설: `Heightmap::at()` 정의를 헤더로 옮겨 호출 지점에서 인라인 가능하게 하면 동일 계산의 실행 시간이 줄어든다.
- 독립변수: accessor 정의 위치만 out-of-line(`6624551`)에서 inline(`0beed39`)으로 변경한다.

## 통제변수

- workload: 64×64 fBm terrain, thermal erosion 10회
- compiler: Apple Clang 17.0.0
- flags: `-std=c++20 -O3 -DNDEBUG -Wall -Wextra`
- hardware: MacBook Air, Apple M2 8-core CPU, 8 GB, arm64
- 반복: variant별 12 runs, run별 10 warmups + 200 samples
- 실행 순서: AB/BA 교대

## 채택 기준

평가 프로토콜 §18의 perf 기준을 사후 판정 기준으로 사용한다. 평균 실행 시간이 10% 이상 감소하고 근사 `t >= 2.5`이면 Accepted로 기록한다.

## 결과

| 지표 | Out-of-line | Inline | 평균 감소 | 근사 t | 판정 |
|---|---:|---:|---:|---:|---|
| thermal erosion only (approx.) | 966.5 ± 101.0 µs | 372.9 ± 17.0 µs | 61.4% | 20.07 | Accepted |
| fBm + thermal erosion (direct) | 2962.5 ± 121.3 µs | 2369.0 ± 37.7 µs | 20.0% | 16.19 | Accepted |

`thermal erosion only`는 각 run의 combined 평균에서 같은 run의 fBm 평균을 뺀 파생치다. 따라서 포트폴리오에서는 직접 측정한 combined 20.0% 감소를 함께 제시한다.

## 실행 정보

- before commit: `66245519242e2739f230c425195ec333bead12ac`
- after commit: `0beed399724a80dcd90dcfe56531cc9100c48227`
- raw JSON: [`../evaluations/perf-heightmap-at-inline__bench-cpp.json`](../evaluations/perf-heightmap-at-inline__bench-cpp.json)
- plot: [`../plots/perf-heightmap-at-inline__metric-thermal-runtime.png`](../plots/perf-heightmap-at-inline__metric-thermal-runtime.png)
- runner: [`../run_inline_accessor_comparison.py`](../run_inline_accessor_comparison.py)

## 결론

동일한 계산 의미를 유지한 채 accessor의 번역 단위 경계가 native C++ workload의 실행 비용에 영향을 주는 것을 반복 측정으로 확인했다. 이 결과는 Python, pybind11, 네트워크, Unity를 포함하지 않는 core-only 주장이다.

