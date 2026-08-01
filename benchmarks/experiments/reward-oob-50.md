# `reward-oob-50` — Out-of-bounds penalty 10 → 50

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-002` |
| Created | `2026-08-01` |
| Planned | `2026-08-01` |
| Started | `—` |
| Completed | `—` |
| Predecessor | `env-maxsteps1000-fmax2` |

## 상태

`Planned`

## 관찰

현재 기준 조건 `env-maxsteps1000-fmax2`의 PPO는 D20에서 20개 episode 중 15개가 맵 밖으로 이탈했다. Unity replay에서는 대부분 목표 방향으로 계속 힘을 가하며 직진했고, 목표에 접근할 때 제동하거나 목표를 빗나간 뒤 재조향하는 행동이 거의 보이지 않았다.

성공한 episode도 정책이 능동적으로 감속·방향 전환했다기보다 직선 진행 경로가 목표 반경과 겹친 경우가 중심이었다.

## 근거

- Baseline experiment: [`env-maxsteps1000-fmax2.md`](env-maxsteps1000-fmax2.md)
- Baseline D20 결과: [`env-maxsteps1000-fmax2__train-s0__eval-dev20__controller-ppo.json`](../evaluations/env-maxsteps1000-fmax2__train-s0__eval-dev20__controller-ppo.json)
- 평가 집합: D20, seed `1000~1019`
- Baseline 결과:
  - 성공: 20% (4/20)
  - 맵 이탈: 75% (15/20)
  - 시간초과: 5% (1/20)
  - 평균 return: `17.68`
- 맵 이탈 15개 중 10개가 양의 return으로 종료
- 맵 이탈 episode의 평균 return: `+3.52`

실패한 직진 episode가 최종적으로 맵을 이탈해도 누적 거리 감소 reward가 `-10` 패널티를 상쇄할 수 있다. 현재 reward는 맵 이탈을 명확한 실패로 구분하지 못한다.

## 가설

맵 이탈 패널티를 목표 성공 보상과 같은 크기로 높이면 실패한 직진 episode의 return이 명확히 낮아지고, PPO가 목표 접근 시 제동하거나 빗나간 뒤 재조향하는 정책을 학습할 것이다.

그 결과 맵 이탈률은 감소하고 성공률은 증가할 것이다. 단순히 움직이지 않아 이탈을 피하는 정책으로 바뀌면 timeout이 증가하므로 개선으로 인정하지 않는다.

## 독립변수

```text
OUT_OF_BOUNDS_PENALTY: 10 → 50
```

## 통제변수

- observation: `[dx, dz, vx, vz, gradX, gradZ]`
- reward의 나머지 항목: 거리 감소량 `- 0.01/step`, 목표 도달 `+50`
- physics: `MASS=1.0`, `DT=1/60`, `F_MAX=2.0`, action L2 cap
- terrain: `MAP_SIZE=64`, `SCALE=10.0`, `TALUS_ANGLE=0.15`, `EROSION_RATE=0.3`, erosion 10 iterations
- episode: `MAX_STEPS=1000`, `GOAL_RADIUS=1.0`, `EDGE_MARGIN=4.0`
- PPO 설정: `env-maxsteps1000-fmax2` metadata와 동일
- training steps: `1,000,000`
- training seed: `0`
- development evaluation: D20 `1000~1019`
- validation evaluation: V100 `2000~2099` — D20 gate 통과 시에만 실행

## 사전 채택 기준

### D20 development gate

다음 조건을 모두 만족해야 V100으로 진행한다.

- 맵 이탈률 `50% 이하` (`10/20` 이하, baseline 대비 최소 `25%p` 감소)
- 성공률 `30% 이상` (`6/20` 이상, baseline 대비 최소 `10%p` 증가)
- 시간초과율 `30% 이하` (`6/20` 이하)
- baseline에서 이탈한 동일 seed의 candidate trajectory 중 적어도 하나에서 목표 접근 감속, 진행 방향 변경 또는 이탈 회피가 관찰됨

맵 이탈만 줄고 timeout이 `30%`를 초과하면 정지·소극 행동으로 실패 유형이 바뀐 것으로 보고 D20 gate를 통과시키지 않는다.

### V100 최종 판정

D20 gate를 통과하면 `env-maxsteps1000-fmax2` baseline과 `reward-oob-50` candidate를 모두 V100에서 평가한다.

`Accepted` 조건:

- candidate 맵 이탈률이 baseline보다 최소 `20%p` 감소
- candidate 성공률이 baseline보다 최소 `10%p` 증가
- candidate 시간초과율 `30% 이하`
- 양의 return으로 끝나는 맵 이탈 episode의 비율이 baseline보다 감소
- 동일 seed trajectory 비교에서 제동 또는 재조향 행동이 확인됨

`Rejected` 조건:

- D20 gate를 통과하지 못함
- 또는 V100에서 위 정량 조건을 충족하지 못하고 실패 유형만 이탈에서 timeout으로 이동함

결과가 정량 기준 경계에 있거나 training seed 분산 가능성을 배제할 수 없으면 `Inconclusive`로 기록한다.

## 실행 정보

- commit SHA: 실행 시 기록
- 실행 명령: 구현 후 기록
- candidate model 예정 위치: `training/artifacts/reward-oob-50/model.zip` (로컬, Git 제외)
- D20 결과 예정 위치: `benchmarks/evaluations/reward-oob-50__train-s0__eval-dev20__controller-ppo.json`
- V100 결과 예정 위치: `benchmarks/evaluations/reward-oob-50__train-s0__eval-val100__controller-ppo.json`

## 결과

아직 구현·학습·평가하지 않았다.

| 지표 | Baseline | Candidate | 변화 |
|---|---:|---:|---:|
| Success rate | D20 20% | — | — |
| Out-of-bounds rate | D20 75% | — | — |
| Timeout rate | D20 5% | — | — |
| Median success steps | D20 341 | — | — |
| Mean return | D20 17.68 | — | — |

## 결론

`Planned` — 결과 없음.

## 다음 질문

`OUT_OF_BOUNDS_PENALTY=50`이 직진·이탈 정책을 억제하면서 실제 제동과 재조향을 유도하는가, 아니면 움직임을 줄여 timeout으로 실패 유형만 바꾸는가?
