# `reward-oob-50` — Out-of-bounds penalty 10 → 50

## 실험 메타데이터

| 필드 | 값 |
|---|---|
| Sequence | `EXP-002` |
| Created | `2026-08-01` |
| Planned | `2026-08-01` |
| Started | `2026-08-01` |
| Completed | `2026-08-01` |
| Predecessor | `env-maxsteps1000-fmax2` |

## 상태

`Rejected`

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

- commit SHA: `9d3d8797e604a5971a0fbf251c9e1592c3c5350d` (working tree dirty)
- 실행 명령: `/Users/widohun/miniconda3/bin/python3 train.py --timesteps 1000000 --seed 0 --out artifacts/reward-oob-50/model --log-dir artifacts/reward-oob-50/logs --tb-log-dir artifacts/reward-oob-50/tb_logs --run-name reward-oob-50 --experiment-id reward-oob-50 --out-of-bounds-penalty 50`
- 모델 위치(로컬, Git 제외): `training/artifacts/reward-oob-50/model.zip`
- D20 평가 명령: `/Users/widohun/miniconda3/bin/python3 eval.py --model artifacts/reward-oob-50/model --controller ppo --episodes 20 --seed-start 1000 --experiment-id reward-oob-50 --out ../benchmarks/evaluations/reward-oob-50__train-s0__eval-dev20__controller-ppo.json --dump-trajectory ../unity-client/Assets/StreamingAssets/reward-oob-50__eval-dev20__replay.json`
- D20 결과: [`reward-oob-50__train-s0__eval-dev20__controller-ppo.json`](../evaluations/reward-oob-50__train-s0__eval-dev20__controller-ppo.json)
- V100: 실행하지 않음 (D20 development gate 실패)

## 결과

| 지표 | Baseline | Candidate | 변화 |
|---|---:|---:|---:|
| Success rate | 20% (4/20) | 15% (3/20) | -5%p |
| Out-of-bounds rate | 75% (15/20) | 60% (12/20) | -15%p |
| Timeout rate | 5% (1/20) | 25% (5/20) | +20%p |
| Median success steps | 341 | 329 | -12 |
| Mean return | 17.68 | -7.05 | -24.73 |

후보는 양의 return으로 끝난 맵 이탈을 `10/15`에서 `0/12`로 제거했다. 그러나 이탈 감소가 성공으로 이어지지 않았고, timeout이 4개 증가했다. D20 replay에서도 제동과 재조향이 일관되게 나타나지 않았으며, 이탈을 피한 episode 일부는 목표에서 먼 상태로 timeout됐다.

`rollout/ep_rew_mean`은 학습 초반 약 `-9.90`에서 최종 `-5.13`까지 개선됐지만, 마지막 10개 기록 평균도 `-1.95`였다. 이 값은 stochastic training rollout의 이동 평균이므로 채택 판단에는 고정 seed deterministic D20 결과를 우선했다.

## 결론

`Rejected`

D20 development gate의 세 조건 중 timeout 조건만 통과했다. 맵 이탈은 기준선보다 `15%p`만 감소해 요구한 `25%p`에 미달했고(`60% > 50%`), 성공률도 `15%`로 요구치 `30%`에 미달하면서 기준선보다 `5%p` 하락했다. penalty 증가는 양의 return 맵 이탈을 제거했지만, 행동을 성공으로 바꾸지 못하고 일부 실패를 timeout으로 이동시켰다. 따라서 사전등록된 규칙에 따라 V100을 실행하지 않는다.

## 다음 질문

`OUT_OF_BOUNDS_PENALTY=30` 같은 중간 penalty가 맵 이탈을 줄이면서 timeout 증가 없이 성공률을 개선할 수 있는지는 열린 질문으로 남지만, 이 실험에서 추적하지 않는다.

**환경 동결 결정 (2026-08-01)**: 이 실험은 이탈률이 줄어든 만큼(-15%p) timeout이 늘어난(+20%p) 실패 유형 전이 사례로, `AGENTS.md` 실험 루프에서 "이탈이 timeout으로 바뀜 → 원인 기록 후 환경 동결" 케이스에 해당한다. reward를 더 튜닝해 정책 성능을 연구 수준으로 다듬는 대신, 여기서 관측·튜닝 대상 환경(`env-maxsteps1000-fmax2`의 물리/지형/reward 구성)을 동결하고 Phase 2c(C++ correctness test + throughput profiling + parallel stepping)로 중심을 옮긴다. RL 쪽 남은 몫은 이 결론을 `docs/`의 RL case study 결과로 정리하는 것뿐이다.
