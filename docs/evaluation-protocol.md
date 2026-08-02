# RL 환경 평가 원칙

이 문서는 `terrain-sim`의 RL 이동 에이전트를 튜닝하고 평가할 때 지켜야 할 공통 원칙을 정의한다. 목표는 학습 reward 하나를 높이는 것이 아니라, **C++ 물리의 지형 제약이 실제로 작동하고 정책이 그 제약 안에서 안정적으로 목표에 도달하는지** 재현 가능한 실험으로 검증하는 것이다.

## 1. 평가 대상과 기준선

전체 경로는 다음과 같다.

```text
C++ physics → pybind11 → Gymnasium → PPO → deterministic evaluation → Unity replay
```

환경 또는 물리를 변경하기 전 현재 모델과 설정을 baseline으로 고정한다. 실험 ID는 `baseline-l2cap`, `reward-oob-50`, `physics-fmax-4`처럼 이름만 보고 변경 목적을 알 수 있게 붙인다.

각 baseline/실험에는 최소한 다음 정보를 기록한다.

- 학습 seed와 총 training steps
- PPO 설정
- 환경 및 물리 상수
- 코드 commit SHA
- 평가 seed 집합
- 성공·맵 이탈·시간초과 결과

기존의 축별 action 제한으로 학습된 모델은 `baseline-axis-cap-legacy`로 보존한다. action을 L2 norm 기준으로 제한한 뒤 다시 학습한 모델은 `baseline-l2cap`으로 사용한다.

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

1. `baseline-axis-cap-legacy` 결과와 아티팩트 보존
2. action L2 norm 제한 적용 후 `baseline-l2cap` 재학습
3. `baseline-l2cap`을 V100에서 평가
4. 지형 경사 분포와 경로 가능성 분석
5. Random/Direct-to-goal/PPO baseline 비교
6. baseline trajectory에서 제동·방향 전환·이탈 실패 원인 분석
7. 물리 자동 테스트 완료
8. 지형 결합이 약하면 `F_MAX` 또는 terrain 난이도를 단일 변수로 조정
9. 실패한 직진도 높은 return을 받는지 확인하고 reward 실험
10. 필요할 때만 observation 정규화와 lookahead 실험
11. 경계 정보 부족이 원인으로 입증된 경우에만 제한적 경계 센서 실험
12. 최종 설정 선택
13. training seed 0/1/2로 각각 재학습
14. T100 최종 평가
15. 대표 성공·우회·실패 trajectory를 Unity에서 재생
16. 설정을 고정한 뒤 Phase 2c 처리량 프로파일링과 병렬화 진행

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

## 12. 실험 운영 전략

모든 환경·reward·physics 튜닝은 다음 순서를 따른다.

```text
관찰 → 근거 수집 → 가설 → 사전 채택 기준 → 단일 변경
→ D20 평가 → 필요 시 V100 평가 → 채택/기각 → 다음 질문
```

### 순서와 일자 기록

의미 기반 experiment ID는 유지하되, 각 실험 문서 안에 별도의 시간 메타데이터를 기록한다.

```text
sequence: EXP-001
created: YYYY-MM-DD
planned: YYYY-MM-DD | N/A
started: YYYY-MM-DD | —
completed: YYYY-MM-DD | —
predecessor: <experiment-id> | none
```

- `sequence`는 실험 문서를 만든 시간 순서이며 `EXP-001`부터 증가한다.
- 순번은 chronology용 metadata일 뿐 파일명이나 experiment ID에 넣지 않는다.
- `created`는 문서 생성일이다.
- `planned`는 가설과 채택 기준을 결과 확인 전에 고정한 날짜다.
- `started`는 코드 변경 또는 학습 실행을 시작한 날짜다.
- `completed`는 평가와 결론 기록까지 끝난 날짜다.
- 소급 작성한 과거 실험은 `planned: N/A (retrospective)`로 표시하고 사전 등록된 것처럼 꾸미지 않는다.
- 날짜는 프로젝트 timezone 기준 ISO 8601 `YYYY-MM-DD` 형식을 사용한다.
- `predecessor`는 이번 질문을 직접 만든 직전 실험을 가리킨다. 단순히 직전 번호를 기계적으로 연결하지 않는다.

### 실험 시작 전

- 먼저 baseline의 수치와 trajectory를 확인한다.
- 변경 전에 가설과 채택 기준을 문장으로 적는다.
- 독립변수 하나와 반드시 유지할 통제변수를 명시한다.
- 결과 파일 이름과 experiment ID를 먼저 정한다.
- T100 결과를 보거나 사용하지 않는다.

### 실험 실행 중

- 학습 실패, 예상 밖 결과, 기각된 후보도 삭제하지 않는다.
- 코드와 실행 설정이 달라졌다면 같은 experiment ID를 재사용하지 않는다.
- 여러 변경을 실수로 함께 적용했다면 탐색 결과로만 취급하고 정식 비교에서 제외한다.
- 정량 결과와 함께 최소 한 개 이상의 대표 trajectory를 확인한다.

### 실험 종료 후

- 결과를 가설에 유리하게 해석하지 않고 사전 채택 기준으로 판정한다.
- 채택/기각/판단 보류 중 하나를 명시한다.
- 결과가 예상과 다르면 새로운 가설을 별도 실험으로 분리한다.
- 다음 실험은 이번 결과가 만든 가장 중요한 질문 하나만 다룬다.
- 최종 README에는 모든 시행착오가 아니라 설계 판단을 바꾼 대표 실험 2~3개만 요약한다.

## 13. 증거의 우선순위

판단 근거는 다음 순서로 신뢰한다.

1. 자동 correctness test
2. 고정 seed 정량 평가
3. controller baseline과의 비교
4. episode trajectory 및 Unity 재생
5. TensorBoard 학습 곡선
6. 주관적인 시각 인상

학습 reward 상승만으로 정책 개선을 주장하지 않는다. 성공률이 높아도 trajectory가 의도한 행동과 다르면 원인을 분석한다. 반대로 시각적으로 그럴듯해도 고정 seed 결과가 개선되지 않으면 채택하지 않는다.

## 14. 변경 유형별 경계

| 변경 유형 | 예시 | 별도 실험 필요 여부 |
|---|---|---|
| correctness fix | action L2 cap, 잘못된 수식 수정 | 새 baseline을 만든 뒤 이후 실험의 전제로 사용 |
| environment tuning | observation, reward, 종료 조건 | 반드시 단일 변수 실험 |
| physics tuning | `F_MAX`, 마찰, timestep | 반드시 단일 변수 실험 및 물리 테스트 동반 |
| task difficulty | terrain scale, erosion 강도 | physics와 분리해 실험 |
| training tuning | PPO hyperparameter, training steps | 환경과 물리 고정 후 수행 |
| instrumentation | 평가 필드·로그 추가 | 동작이 바뀌지 않으면 기존 baseline 유지 가능 |

Correctness fix 전후 모델은 같은 baseline으로 섞지 않는다. 예를 들어 축별 action cap에서 학습한 `baseline-axis-cap-legacy`와 L2 cap에서 학습한 `baseline-l2cap`은 별도 결과로 취급한다.

## 15. 실험 기록 위치

```text
benchmarks/
├── evaluations/   # episode별 원시 JSON/CSV
├── experiments/   # 가설, 변경, 결과, 결론을 담은 Markdown
└── plots/         # 비교 그래프와 공개 문서용 이미지
```

각 실험 문서는 `benchmarks/EXPERIMENT_TEMPLATE.md`의 템플릿을 사용한다. 원시 결과와 실험 문서는 같은 experiment ID를 공유해야 한다.

## 16. Plot 작성 원칙

Plot은 결과를 장식하는 이미지가 아니라, 실험의 질문 하나에 답하는 증거다.

### 필수 원칙

- 하나의 plot은 하나의 질문만 다룬다.
- 모든 plot은 생성에 사용한 JSON/CSV와 experiment ID를 명시한다.
- 비교 대상은 같은 evaluation seed 집합과 같은 평가 조건을 사용한다.
- 성공한 seed나 보기 좋은 trajectory만 골라 전체 성능처럼 제시하지 않는다.
- 축 범위를 잘라 차이를 과장하지 않는다. 불가피하면 잘린 축임을 명확히 표시한다.
- 여러 training seed 결과는 평균만 표시하지 않고 개별 값 또는 분산/error bar를 함께 표시한다.
- smoothing을 적용했다면 window와 방식을 표시하고, 가능하면 원본 곡선도 희미하게 함께 표시한다.
- 색만으로 범주를 구분하지 않고 label, marker 또는 line style을 함께 사용한다.
- 제목에는 결론을 과장하지 않고 측정 대상을 적는다.
- 단위, seed 수, 평가 집합(D20/V100/T100)을 그림 안이나 caption에 표시한다.

### 권장 plot

| Plot | 답해야 할 질문 |
|---|---|
| controller outcome 비교 | PPO가 Random/Direct controller보다 실제로 우수한가? |
| baseline vs candidate outcome 비교 | 변경이 성공·이탈·timeout 비율을 어떻게 바꿨는가? |
| success rate by training seed | 개선이 특정 학습 seed에만 의존하는가? |
| 경사각 분포와 `θmax` | 지형에 실제 등반 불가 영역이 존재하는가? |
| 대표 trajectory top-down plot | 정책이 직진·제동·방향 전환·우회를 어떻게 수행했는가? |
| 거리·속도·action 시계열 | 목표 접근 중 제동이 실제로 발생했는가? |
| throughput scaling | env 수 증가가 steps/sec와 효율에 어떤 영향을 줬는가? |

### 대표 trajectory 선정 규칙

trajectory는 정량 평가를 보완하는 사례이지 전체 결과의 대체물이 아니다.

- seed와 선정 이유를 caption에 쓴다.
- 성공·대표 실패·경계 사례를 구분한다.
- 가능하면 median episode 또는 사전에 정한 seed를 사용한다.
- 가장 보기 좋은 성공 사례만 골랐다면 `best case`라고 명시한다.
- before/after 비교는 동일한 terrain/start/goal seed를 사용한다.

### 파일과 재현성

```text
benchmarks/plots/<experiment-id>__eval-<set>__metric-<metric>.png
benchmarks/plots/<experiment-id>__eval-<set>__metric-<metric>.svg
```

- README에는 호환성이 좋은 PNG를 사용한다.
- 선·텍스트 기반 그래프는 가능하면 SVG도 함께 생성한다.
- plot 생성 스크립트는 Git에 포함하고 수동 편집으로 수치를 바꾸지 않는다.
- 그림만 보고도 평가 집합, episode 수, 단위와 비교 대상을 알 수 있어야 한다.
- 최종 README에는 핵심 주장에 직접 필요한 plot만 3~5개 사용한다.

## 17. 실험 및 아티팩트 명명 규칙

`b0`, `e1`, `r1`, `p1`처럼 문서를 열어야 뜻을 알 수 있는 순번 전용 이름은 새 실험에 사용하지 않는다. 이름만 보고 변경 목적, 학습 seed, 평가 집합과 controller를 식별할 수 있어야 한다.

### 공통 형식

- 모두 소문자 ASCII를 사용한다.
- 단어 내부는 kebab-case(`reward-oob-50`)를 사용한다.
- 서로 다른 메타데이터 차원은 이중 underscore(`__`)로 구분한다.
- 공백, 대문자, 날짜만으로 된 이름은 사용하지 않는다.
- 파일 이름에 모든 hyperparameter를 넣지 않는다. 전체 설정은 결과 JSON과 실험 문서에 기록한다.

### Experiment ID

```text
<category>-<changed-variable>-<value-or-purpose>
```

권장 category:

| Category | 용도 | 예시 |
|---|---|---|
| `baseline` | 비교 기준 | `baseline-l2cap` |
| `reward` | reward 변경 | `reward-oob-50` |
| `obs` | observation 변경 | `obs-normalized` |
| `physics` | 물리 파라미터·동작 변경 | `physics-fmax-4` |
| `terrain` | 지형 난이도 변경 | `terrain-scale-6` |
| `train` | PPO/학습 설정 변경 | `train-steps-2m` |
| `perf` | 성능 실험 | `perf-parallel-envs` |

Experiment ID에는 한 실험의 독립변수 하나만 표현한다. 같은 의미의 실험을 다시 실행하되 구현이나 조건이 달라졌다면 `-v2`를 붙이고, 단순 재실행은 training seed나 run 번호로 구분한다.

### 평가 집합 토큰

| 문서상 이름 | 파일 토큰 | Seed |
|---|---|---:|
| D20 | `eval-dev20` | `1000~1019` |
| V100 | `eval-val100` | `2000~2099` |
| T100 | `eval-test100` | `3000~3099` |

`dev`, `val`, `test`의 의미가 파일 이름에 드러나게 하고 `d20`, `v100`, `t100`만 단독으로 쓰지 않는다.

### 파일 및 디렉터리 형식

PPO 모델과 raw log 디렉터리:

```text
training/artifacts/<experiment-id>__train-s<seed>/
```

평가 결과:

```text
benchmarks/evaluations/<experiment-id>__train-s<seed>__eval-<set>__controller-ppo.json
benchmarks/evaluations/<experiment-id>__eval-<set>__controller-<direct|random>.json
```

실험 문서:

```text
benchmarks/experiments/<experiment-id>.md
```

Plot:

```text
benchmarks/plots/<experiment-id>__eval-<set>__metric-<metric>.png
benchmarks/plots/<experiment-id>__eval-<set>__metric-<metric>.svg
```

예시:

```text
training/artifacts/reward-oob-50__train-s0/
benchmarks/experiments/reward-oob-50.md
benchmarks/evaluations/reward-oob-50__train-s0__eval-dev20__controller-ppo.json
benchmarks/evaluations/reward-oob-50__train-s0__eval-val100__controller-ppo.json
benchmarks/evaluations/baseline-l2cap__eval-val100__controller-direct.json
benchmarks/plots/reward-oob-50__eval-val100__metric-outcomes.png
```

### 결과 내부 식별자

평가 JSON에는 파일 이름과 별도로 다음 메타데이터를 저장한다.

```text
experiment_id
training_seed
training_steps
evaluation_set
evaluation_seed_start
evaluation_seed_end
controller
commit_sha
```

파일 이름은 사람이 찾기 위한 색인이고 JSON metadata가 최종 진실의 원천이다. 파일 이름과 metadata가 다르면 결과를 정식 비교에서 제외한다.

### 기존 파일 처리

초기의 `b0/b1/b2` 이름은 의미 기반 이름으로 마이그레이션했다. 대체된 실행도 파일 이름에 `legacy`를 붙이지 않고 실제 조건으로 식별하며, `superseded` 여부는 metadata와 실험 문서에 기록한다.

## 18. `perf` 실험의 통계적 판단 기준

RL episode 평가(성공/맵 이탈/시간초과)는 §2의 D20/V100/T100 seed 집합과 이항분포 기반 불확실성(§13)으로 판단 기준을 갖는다. `perf` 카테고리 성능 실험(예: `perf-parallel-envs`, `perf-perlin-table-reuse`)은 episode가 아니라 처리량(steps/sec, resets/sec) 같은 연속값을 반복 측정하므로 별도 기준이 필요하다.

**개선폭을 라운드 넘버(“+30% 이상”처럼)로 사전 등록하는 것만으로는 부족하다** — 반복 측정에 실제로 상당한 분산이 있는데(스레드 스케줄링, 캐시 상태 등), 그 분산을 반영하지 않은 임계값은 노이즈를 개선으로 착각하거나 진짜 개선을 노이즈로 착각할 수 있다.

### 방법

thread_count당 반복 횟수 `n`(perf 실험 기본값 7)에 대해, baseline/candidate 각각의 평균 `mean`과 표준편차 `sd`가 있다고 하자.

```text
SE_baseline  = sd_baseline / sqrt(n)
SE_candidate = sd_candidate / sqrt(n)
SE_diff      = sqrt(SE_baseline^2 + SE_candidate^2)
t            = (mean_candidate - mean_baseline) / SE_diff
pct_change   = (mean_candidate - mean_baseline) / mean_baseline * 100
```

`n=7` 수준의 표본에서 `|t| >= 2.5`는 대략 양측 `p<0.05`에 해당하는 실무적 문턱으로 쓴다(엄밀한 Welch–Satterthwaite 자유도 계산 대신 쓰는 근사치임을 명시한다).

### 판정 (지표별로 따로 판정한다 — steps/sec와 resets/sec를 하나로 뭉쳐 판정하지 않는다)

- **Accepted**: `|t| >= 2.5`(노이즈로 설명 안 됨) **그리고** 방향이 개선 **그리고** `|pct_change| >= 10%`(통계적으로 유의하지만 실무적으로 무의미한 차이를 걸러냄).
- **Rejected**: `|t| < 1.0`(신호 없음, 노이즈와 구별 안 됨) — 점 추정치의 방향과 무관하게 "차이 없음"으로 본다. `|t| >= 2.5`인데 방향이 악화인 경우도 Rejected.
- **Inconclusive**: 그 사이(`1.0 <= |t| < 2.5`), 또는 통계적으로 유의하지만 `pct_change`가 10% 미만인 경우.

지표별 판정이 갈리면(예: resets/sec는 Accepted, steps/sec는 Rejected) 실험 전체를 하나의 라벨로 억지로 합치지 않고 지표별로 결론을 따로 기술한다.
