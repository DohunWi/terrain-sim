# `physics-integrator-energy-drift` — 운영 dt에서 적분기 수치 거동 비교

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-011` |
| Created | `2026-08-08` |
| Planned | `N/A (retrospective evidence capture)` |
| Started | `2026-08-08` |
| Completed | `2026-08-08` |
| Predecessor | `none` |

## 상태

`Accepted` — 현재 선택을 뒷받침하는 결정론적 수치 비교이며 wall-clock 성능 실험은 아니다.

## 질문

현재 rigid-body step이 사용하는 semi-implicit Euler는 운영 `dt=1/60 s`에서 explicit Euler보다 안정적인가? 더 높은 force-evaluation 비용의 Verlet/RK4와 비교하면 어떤 trade-off가 있는가?

## 통제 조건

- model: 1D harmonic oscillator, `k=1`, `m=1`
- initial state: `x=1`, `v=0`
- duration: 20 periods
- dt sweep: `0.001`, `1/60`, `0.05`, `0.1`, `0.2`
- metric: initial energy 대비 최대·최소·최종 drift

## 운영 dt 결과

| 적분기 | Force evaluations / step | 최대 절대 drift |
|---|---:|---:|
| Explicit Euler | 1 | 711.6% |
| Semi-implicit Euler | 1 | 0.8403% |
| Velocity Verlet | 2 | 0.00692% |
| RK4 | 4 | 0.0003695% |

## 실행 정보

- commit: `0beed399724a80dcd90dcfe56531cc9100c48227`
- raw JSON: [`../evaluations/physics-integrator-energy-drift__bench-cpp.json`](../evaluations/physics-integrator-energy-drift__bench-cpp.json)
- plot: [`../plots/physics-integrator-energy-drift__metric-operating-dt.png`](../plots/physics-integrator-energy-drift__metric-operating-dt.png)
- runner: [`../run_integrator_evidence.py`](../run_integrator_evidence.py)

## 결론과 제한

격리 모델에서는 semi-implicit Euler가 explicit Euler와 같은 1회 force evaluation 비용으로 에너지 발산을 피했다. Verlet/RK4는 drift가 더 작지만 force evaluation이 2배/4배다. 이 결과는 현재 선택의 방향을 지지하지만, terrain contact·마찰·충돌을 포함하지 않으므로 실제 rigid-body 정확도의 일반적 증명으로 제시하지 않는다.

