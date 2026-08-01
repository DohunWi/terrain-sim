# `env-maxsteps1000-fmax2` — Episode step budget 500 → 1000

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-001` |
| Created | `2026-08-01` |
| Planned | `N/A (retrospective)` |
| Started | `2026-08-01` |
| Completed | `2026-08-01` |
| Predecessor | `explore-fmax2-talus015-steps500` |

## 상태

`Inconclusive`

이 문서는 실험 완료 후 도입된 protocol에 맞춰 소급 작성했다. 실행 전에 채택 기준을 사전 등록하지 않았으므로 정식 `Accepted` 판정을 내리지 않는다.

## 관찰

`F_MAX=2.0`, `TALUS_ANGLE=0.15`, `MAX_STEPS=500` 조건에서 PPO의 D20 평가 결과 20개 중 11개가 시간초과했다. 성공한 episode도 중앙값 378 step이 필요해 500-step 예산이 우회 이동과 제동을 학습하기에 부족할 가능성이 있었다.

## 근거

- Baseline 결과: [`explore-fmax2-talus015-steps500__train-s0__eval-dev20__controller-ppo.json`](../evaluations/explore-fmax2-talus015-steps500__train-s0__eval-dev20__controller-ppo.json)
- Candidate 결과: [`env-maxsteps1000-fmax2__train-s0__eval-dev20__controller-ppo.json`](../evaluations/env-maxsteps1000-fmax2__train-s0__eval-dev20__controller-ppo.json)
- 평가 집합: D20, seed `1000~1019`
- 학습: PPO, training seed `0`, 각 `1,000,000` steps
- Unity D20 replay 수동 확인: 대부분 목표 방향으로 계속 힘을 가하며 직진했고, 목표 접근 시 제동하거나 빗나간 뒤 재조향하는 행동이 거의 보이지 않았다. 성공 episode도 직선 진행 경로가 목표 반경과 겹치는 경우가 중심이었다.

Candidate의 15개 맵 이탈 episode 중 10개가 양의 return으로 종료됐고, 맵 이탈 episode의 평균 return도 `+3.52`였다. 실패한 직진이 reward 관점에서 충분히 불리하지 않다는 신호다.

## 가설

최대 episode 길이를 500에서 1000 step으로 늘리면 약한 힘과 지형 우회 때문에 필요한 이동 시간을 확보해 시간초과가 감소하고 성공률이 증가할 것이다.

## 독립변수

```text
MAX_STEPS: 500 → 1000
```

## 통제변수

- observation: `[dx, dz, vx, vz, gradX, gradZ]`
- reward: 거리 감소량 `- 0.01/step`, 목표 `+50`, 맵 이탈 `-10`
- physics: `MASS=1.0`, `DT=1/60`, `F_MAX=2.0`, action L2 cap
- terrain: `MAP_SIZE=64`, `SCALE=10.0`, `TALUS_ANGLE=0.15`, erosion 10 iterations
- PPO 설정: stable-baselines3 기본 PPO 설정, 각 model metadata 참고
- training steps: `1,000,000`
- training seed: `0`
- evaluation seeds: D20 `1000~1019`

## 사전 채택 기준

실험 당시에 사전 등록된 채택 기준이 없었다. 이는 현재 protocol 기준의 한계이며, 결과를 `Inconclusive`로 분류하는 이유 중 하나다.

소급 비교에서는 다음을 확인한다.

- 시간초과가 실제로 감소했는가?
- 성공률도 함께 증가했는가?
- 시간 증가가 다른 실패 유형으로 단순 전환되지 않았는가?
- trajectory에서 추가 시간이 제동·재조향·우회 행동으로 사용됐는가?

## 실행 정보

- 당시 commit SHA: `5d7b75717cb0687258b7f9b458a086e5574ed245`
- 당시 working tree: dirty
- 정확한 실행 명령: 별도 보존되지 않음
- baseline model: `training/artifacts/explore-fmax2-talus015-steps500/model.zip` (로컬, Git 제외)
- candidate model: `training/artifacts/env-maxsteps1000-fmax2/model.zip` (로컬, Git 제외)
- 평가 결과: 위 D20 JSON 두 개

정확한 명령을 보존하지 못한 문제는 이후 `train.py`의 `meta.json`과 정식 experiment 사전 등록으로 방지한다.

## 결과

| 지표 | Baseline (`MAX_STEPS=500`) | Candidate (`MAX_STEPS=1000`) | 변화 |
|---|---:|---:|---:|
| Success rate | 30% (6/20) | 20% (4/20) | -10%p |
| Out-of-bounds rate | 15% (3/20) | 75% (15/20) | +60%p |
| Timeout rate | 55% (11/20) | 5% (1/20) | -50%p |
| Median success steps | 378 | 341 | -37 |
| Mean episode length | 455.35 | 462.05 | +6.70 |
| Mean return | 36.12 | 17.68 | -18.44 |

`MAX_STEPS=1000`은 의도대로 시간초과를 크게 줄였다. 그러나 성공률은 개선되지 않았고, 제거된 시간초과가 대부분 맵 이탈로 바뀌었다. 추가 시간이 우회나 안정적인 목표 접근보다 기존 직진 행동을 더 오래 지속하는 데 사용됐다.

## 결론

`Inconclusive`

500-step 예산이 부족했다는 가설은 시간초과 감소로 일부 지지된다. 하지만 1000-step 조건은 전체 task 성능을 개선하지 못했다. 이번 결과만으로 `MAX_STEPS=1000`을 최종 채택하거나 500으로 되돌릴 수 없다. 먼저 직진 가속과 제동 실패의 원인을 해결한 뒤 episode budget을 다시 비교해야 한다.

## 다음 질문

맵 이탈 패널티가 `10`에 불과해 실패한 직진 episode도 양의 return을 얻는 reward 구조가 제동·재조향 학습을 방해하는가?

다음 실험 후보는 다른 조건을 유지하고 `OUT_OF_BOUNDS_PENALTY: 10 → 50`만 변경하는 것이다. 이 변경은 아직 구현하거나 학습하지 않았다.
