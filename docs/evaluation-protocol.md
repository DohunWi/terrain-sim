# RL 환경 평가 원칙

이 문서는 `terrain-sim`의 RL 이동 에이전트를 튜닝하고 평가할 때 지켜야 할 공통 원칙을 정의한다. 목표는 학습 reward 하나를 높이는 것이 아니라, **C++ 물리의 지형 제약이 실제로 작동하고 정책이 그 제약 안에서 안정적으로 목표에 도달하는지** 재현 가능한 실험으로 검증하는 것이다.

## 1. 평가 대상과 기준선

전체 경로는 다음과 같다.

```text
C++ physics → pybind11 → Gymnasium → PPO → deterministic evaluation → Unity replay
```

환경 또는 물리를 변경하기 전 현재 모델과 설정을 baseline으로 고정한다. 실험 ID는 `b0`, `e1_boundary_obs`, `p1_fmax4`처럼 설정과 결과를 연결할 수 있게 붙인다.

각 baseline/실험에는 최소한 다음 정보를 기록한다.

- 학습 seed와 총 training steps
- PPO 설정
- 환경 및 물리 상수
- 코드 commit SHA
- 평가 seed 집합
- 성공·맵 이탈·시간초과 결과

기존의 축별 action 제한으로 학습된 모델은 `b0_legacy`로 보존한다. action을 L2 norm 기준으로 제한한 뒤 다시 학습한 모델부터 정식 `b0`로 사용한다.

## 2. Seed 집합 분리

| 집합 | Seed | 용도 |
|---|---:|---|
| D20 | `1000~1019` | 개발 중 빠른 회귀 평가와 명백히 나쁜 후보 제거 |
| V100 | `2000~2099` | baseline 및 채택 후보의 정식 비교 |
| T100 | `3000~3099` | 최종 설정 선택 후 마지막 일반화 평가 |

- D20은 반복해서 사용해도 된다.
- V100은 baseline과 채택 가능성이 있는 후보에만 사용한다.
- T100은 최종 설정과 학습 절차를 모두 고정한 후 한 번만 사용한다.
- 매 평가마다 새 random seed를 뽑지 않는다. 모든 후보는 같은 seed 집합에서 비교한다.
- 20개 평가 결과는 탐색용이며 최종 성능 주장에 사용하지 않는다.

## 3. 단일 변수 원칙

한 실험에서는 한 종류의 변수만 바꾼다.

- observation을 바꿀 때 reward, physics, PPO 설정을 유지한다.
- reward를 바꿀 때 observation과 physics를 유지한다.
- `F_MAX`를 바꿀 때 terrain 생성 파라미터와 reward를 유지한다.
- 여러 변경이 필요한 경우 각각의 효과를 확인한 뒤 조합한다.

실험은 `가설 → 변경 → D20 → V100 → 채택/기각` 순서로 진행한다. 평균 reward만으로 변경을 채택하지 않는다.

## 4. 공통 평가 지표

정책 평가는 deterministic action으로 실행하며 다음 값을 기록한다.

- goal success count/rate
- out-of-bounds count/rate
- timeout count/rate
- 전체 평균·중앙 episode length
- 성공 에피소드의 평균·중앙 episode length
- 종료 유형별 평균 final distance
- 평균 episode return
- episode별 최대·평균 경사각

에피소드별 결과에는 최소한 다음 필드를 저장한다.

```text
seed, outcome, episode_steps, return, final_distance,
start_x, start_z, goal_x, goal_z,
max_slope_deg, mean_slope_deg
```

콘솔 출력만 남기지 않고 `benchmarks/evaluations/` 아래 JSON 또는 CSV로 저장한다. 성공률이 약 50%일 때 100개 평가의 불확실성도 대략 ±10%p 수준이므로, 몇 %p 차이만으로 개선이라고 단정하지 않는다. 최종 후보는 여러 training seed에서도 같은 경향을 보여야 한다.

## 5. Controller baseline

PPO는 같은 seed 집합에서 다음 controller와 비교한다.

1. **Random controller**: 매 step 무작위 action
2. **Direct-to-goal controller**: 항상 목표의 수평 방향으로 최대 힘 적용
3. **PPO policy**: 학습된 deterministic policy

Direct-to-goal도 높은 성공률을 보이면 환경이 우회 학습을 요구하지 않는다는 뜻이다. PPO가 direct controller보다 일관되게 우수하고, Unity trajectory에서 실제 우회 행동이 확인되어야 terrain-coupled navigation이라는 주장이 성립한다.

## 6. 힘 제한과 최대 등반각

환경의 action force는 월드 수평면에만 존재한다. 경사각을 `θ`, 오르막 방향의 수평 힘을 `F`, 질량을 `m`이라 하면 접선 방향 힘은 `F cosθ`, 중력의 접선 성분은 `mg sinθ`다.

```text
F cosθ ≥ mg sinθ
F ≥ mg tanθ
θmax = atan(F_MAX / (m g))
```

따라서 `asin(F_MAX / mg)`를 사용하지 않는다.

`F_MAX`가 물리적으로 방향에 무관한 최대 힘이 되려면 action을 축별로만 clip해서는 안 된다. 축별 `[-1, 1]` 제한은 대각선에서 `sqrt(2) × F_MAX`를 허용한다. action은 먼저 L2 norm이 1을 넘지 않게 제한한 뒤 `F_MAX`를 곱한다.

```python
action = np.asarray(action, dtype=np.float32)
norm = np.linalg.norm(action)
if norm > 1.0:
    action = action / norm
force_xz = action * F_MAX
```

`m=1`, `g=9.8`일 때 기준값은 다음과 같다.

| 실험 | `F_MAX` | 최대 등반각 |
|---|---:|---:|
| P1 | 4.0 | 약 22.2° |
| P2 | 5.0 | 약 27.0° |
| P3 | 6.0 | 약 31.5° |

## 7. 지형 난이도 검증

각 지형 셀의 경사 크기와 경사각은 다음과 같이 계산한다.

```text
slope = sqrt(gradX² + gradZ²)
angle = atan(slope)
```

최소 100개 terrain seed에서 다음을 측정한다.

- 경사각 p50, p90, p95, p99, max
- `θmax`보다 가파른 셀의 비율
- start-goal 직선 경로가 등반 불가 경사를 만나는 비율
- start와 goal 사이에 우회 가능한 경로가 존재하는 비율

원하는 상태는 일부 직선 경로가 막히지만 대부분의 start-goal 쌍에는 우회로가 존재하는 것이다. 통과 불가능한 경사가 거의 없으면 평면 navigation과 다르지 않고, 우회로 자체가 없으면 RL 성능 문제가 아니라 task 생성 문제다.

## 8. 물리 자동 검증

정책 평가와 별도로 다음 조건을 자동 테스트한다.

- 평지·무힘·무중력에서 속도 보존
- 자유낙하 결과가 semi-implicit Euler 예측값과 일치
- 일정 경사·무힘에서 접선 가속도가 `g sinθ`와 일치
- `F < mg tanθ`에서 정지 상태로 시작한 물체가 지속적으로 등반하지 못함
- `F > mg tanθ`에서 오르막 가속과 지속 등반이 가능함
- 충돌 이후 외력이 없을 때 에너지가 비물리적으로 증가하지 않음
- 동일 initial state와 action sequence가 같은 결과를 생성
- 지형 경계에서 NaN 또는 범위 밖 메모리 접근이 발생하지 않음

등반 테스트는 초기 접선 속도 0, 일정한 평면 경사, 최대 경사 상승 방향의 힘, 충분한 simulation time을 사용한다. `F < mg tanθ`여도 초기 운동에너지로 잠시 상승할 수 있으므로 한 step의 위치 변화만으로 판정하지 않는다.

## 9. 튜닝 순서

1. `b0_legacy` 결과와 아티팩트 보존
2. action L2 norm 제한 적용 후 정식 `b0` 재학습
3. B0를 V100에서 평가
4. 지형 경사 분포와 경로 가능성 분석
5. Random/Direct-to-goal/PPO baseline 비교
6. 경계 거리 observation 실험
7. 필요할 때만 observation 정규화와 lookahead 실험
8. observation을 고정한 뒤 reward 실험
9. 물리 자동 테스트 완료
10. `F_MAX` 4/5/6 sweep
11. 최종 설정 선택
12. training seed 0/1/2로 각각 재학습
13. T100 최종 평가
14. 대표 성공·우회·실패 trajectory를 Unity에서 재생
15. 설정을 고정한 뒤 Phase 2c 처리량 프로파일링과 병렬화 진행

## 10. 최종 채택 기준

최종 설정은 다음 조건을 함께 만족해야 한다.

- PPO가 direct-to-goal controller보다 일관되게 우수함
- 여러 training seed에서 성공률 개선이 재현됨
- 맵 이탈과 timeout이 특정 실패 유형으로 과도하게 집중되지 않음
- 등반 불가 경사와 우회 가능한 경로가 모두 실제로 존재함
- `mg tanθ` 경계가 물리 자동 테스트로 검증됨
- Unity 재생에서 명확한 우회 성공 사례를 확인할 수 있음

잠정 목표는 training seed 3개의 T100 평균 성공률 75% 이상, 평균 out-of-bounds 10% 이하로 둔다. 단, 지형을 평탄화하거나 task 난이도를 제거해서 수치만 맞추지 않는다.

## 11. 아티팩트 보관

Git에 포함한다.

- 실험 설정과 코드
- 평가 JSON/CSV
- 요약 Markdown과 그래프
- 분석·벤치마크 스크립트
- 최종 대표 trajectory JSON

Git에 포함하지 않는다.

- PPO `.zip` 모델
- TensorBoard raw log
- 임시 checkpoint
- 대량의 replay 파일

모델과 raw log는 `training/artifacts/<experiment-id>/` 같은 로컬 디렉터리에 실험 ID별로 보관한다. Git에는 같은 ID를 가진 구조화된 평가 결과를 남겨 모델 없이도 실험 조건과 결론을 검토할 수 있게 한다.
