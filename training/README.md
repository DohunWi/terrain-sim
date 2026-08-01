# training/

Phase 2b/2c: `core/`(C++ 코어) 위에 얹는 Gymnasium 환경(`env.py`) + stable-baselines3 학습(`train.py`)/평가(`eval.py`)/지형 난이도 분석(`analyze_terrain.py`) 스크립트. `core/src/bindings/`가 제공하는 `terrain_sim_py` 모듈을 primitive로 써서, 여기서 실제 RL task(observation/action space, reward, 에피소드 종료 조건, 도메인 랜덤화)를 정의한다.

**튜닝/평가 방법론은 `docs/evaluation-protocol.md`가 기준 문서** — seed 집합 분리(D20/V100/T100), 단일 변수 원칙, controller baseline, 아티팩트 보관 규칙 전부 거기 있음. 여기 README는 스크립트 사용법만.

핵심 설계:

- **terrain-coupling의 메커니즘**: 관절/회전이 없는 최소 에이전트라 "전복"은 없음 — action force는 항상 수평이고 L2 norm으로 방향 무관하게 `F_MAX`로 제한됨(`docs/evaluation-protocol.md` §6). 경사각 `θ`에서 접선 방향으로 쓸 수 있는 힘은 `F·cosθ`뿐이라, `F_MAX < m·g·tanθ`인 경사는 물리적으로 못 올라감(`theta_max = atan(F_MAX/(m·g))` — `asin`이 아님, 유도 과정은 프로토콜 문서 참고). 별도 경사 페널티 없이 물리 자체가 막는다.
- **observation**: `[dx, dz, vx, vz, gradX, gradZ]` — 목표까지 상대 위치 + 속도 + 현재 위치의 지형 기울기(지형을 넓게 보는 lookahead·경계 거리는 아직 없음, 튜닝 후보)
- **action**: 수평 2D 힘, L2 norm으로 clip 후 `F_MAX` 스케일 (축별 clip 아님 — 축별로 하면 대각선에서 `F_MAX·√2`까지 나가서 힘 제한이 방향 무관하지 않게 됨)
- **reward**: dense shaping(거리 변화량 - 시간 페널티) + 목표 도달/맵 이탈 시 큰 보너스/페널티
- **종료**: 목표 도달·맵 이탈 = `terminated`, `MAX_STEPS` 도달 = `truncated`
- **도메인 랜덤화**: `reset(seed=...)`마다 fbm + 얕은 thermal erosion으로 지형 재생성, start/goal도 같은 시드로 재배치 (재현성 확인됨)

`TerrainAgentEnv(...)` 생성자는 `map_size`/`scale`/`octaves`/`persistence`/`lacunarity`/`talus_angle`/`erosion_rate`/`erosion_iterations`/`f_max`/`max_steps`/`out_of_bounds_penalty`를 override 가능하다(기본값은 클래스 상수). `train.py`와 `eval.py`는 같은 CLI 플래그로 이 값을 노출하며, `eval.py`는 model의 `meta.json` 값을 기본으로 사용한다.

## 환경 설정

pybind11/gymnasium/stable-baselines3/tensorboard는 conda `base`(3.13.5, `~/miniconda3`)에 설치돼 있음 — 이 머신은 pyenv shim이 PATH에서 conda보다 먼저 오니, `training/` 스크립트를 실행할 땐 `~/miniconda3/bin/python3`를 명시해야 함 (VS Code에서도 인터프리터를 이걸로 선택해야 import 경고가 없어짐). 버전은 `requirements.txt` 참고.

```bash
# core/에서 바인딩 빌드 (한 번, 또는 core/ 코드 바뀔 때마다)
cd core && cmake -S . -B build && cmake --build build --target terrain_sim_py

# training/에서 실행 (env.py가 core/build/python을 자동으로 sys.path에 추가함)
cd ../training
/Users/widohun/miniconda3/bin/python3 train.py --timesteps 1000000 --seed 0 \
  --out artifacts/baseline-l2cap-fmax5/model \
  --log-dir artifacts/baseline-l2cap-fmax5/logs \
  --tb-log-dir artifacts/baseline-l2cap-fmax5/tb_logs \
  --run-name baseline-l2cap-fmax5 --experiment-id baseline-l2cap-fmax5
```

`train.py`는 모델과 함께 `<out 디렉터리>/meta.json`을 자동으로 쓴다 — training seed/steps, PPO 하이퍼파라미터(모델 객체에서 실제 값을 읽음, 하드코딩 아님), env 상수 전부, 그리고 그 시점의 git commit SHA(+dirty 여부). `eval.py`는 이 파일을 자동으로 읽어서 **학습 때와 똑같은 env 설정으로** 평가한다 — 실수로 다른 파라미터로 평가하는 걸 막는 안전장치.

## 학습 진행 모니터링 (TensorBoard)

`train.py`는 `--tb-log-dir`(실험별로 분리 권장, 예: `artifacts/baseline-l2cap-fmax5/tb_logs`)에 스칼라 로그를 남긴다. 물리를 매 스텝 렌더링하는 게 아니라 `ep_rew_mean`/`ep_len_mean` 같은 숫자만 기록하는 거라 학습 자체를 늦추지 않는다.

```bash
/Users/widohun/miniconda3/bin/python3 -m tensorboard.main \
  --logdir artifacts/baseline-l2cap-fmax5/tb_logs --port 6006
# http://localhost:6006 에서 SCALARS 탭
```

## 정책 평가

`eval.py`는 고정 seed 집합(D20: 1000~1019, V100: 2000~2099, T100: 3000~3099 — `docs/evaluation-protocol.md` §2)을 deterministic하게 굴려서 성공/맵 이탈/시간초과, 경사각 통계, return 등을 집계한다. `--controller`로 학습된 PPO 정책뿐 아니라 direct-to-goal/random baseline도 같은 코드 경로로 평가 가능(§5). `--experiment-id`는 필수 — 결과 파일의 1차 식별자는 파일 경로가 아니라 이 ID.

```bash
/Users/widohun/miniconda3/bin/python3 eval.py \
  --model artifacts/baseline-l2cap-fmax5/model --controller ppo \
  --episodes 100 --seed-start 2000 --experiment-id baseline-l2cap-fmax5 \
  --out ../benchmarks/evaluations/baseline-l2cap-fmax5__train-s0__eval-val100__controller-ppo.json
```

`--dump-trajectory <path>`를 추가하면 평가한 모든 에피소드의 지형+궤적+결과를 유니티가 읽을 수 있는 JSON으로도 저장한다 (`unity-client/Assets/Scripts/Replay/TrajectoryReplay.cs`가 소비 — 소켓 없는 정적 파일 로딩, `core/src/net/`과 무관). 예:

```bash
/Users/widohun/miniconda3/bin/python3 eval.py \
  --model artifacts/baseline-l2cap-fmax5/model --episodes 20 \
  --experiment-id baseline-l2cap-fmax5 \
  --dump-trajectory ../unity-client/Assets/StreamingAssets/baseline-l2cap-fmax5__eval-dev20__replay.json
```

유니티 씬에 빈 GameObject 만들어서 `TrajectoryReplay` 컴포넌트 붙이면 왼쪽에 에피소드 목록(초록=성공/빨강=맵 이탈/노랑=시간초과)이 뜨고 클릭해서 원하는 에피소드를 재생할 수 있다.

## 지형 난이도 분석

`analyze_terrain.py`는 학습 없이(수 초 내) `TerrainAgentEnv.reset()`이 실제로 만드는 지형·start·goal을 N개 시드에서 반복해서, 셀별 경사각 분포와 `theta_max`(위 참고)보다 가파른 셀 비율, start-goal 직선 경로가 막히는 비율, BFS로 우회 경로가 존재하는 비율을 측정한다. 지형/`F_MAX` 조합을 정책 재학습 없이 빠르게 스캔하는 용도.

```bash
/Users/widohun/miniconda3/bin/python3 analyze_terrain.py --n-terrains 100 --scale 5.0 --f-max 3.0
```

## 아티팩트 보관 (`docs/evaluation-protocol.md` §11)

모델(`.zip`)/raw TensorBoard 로그/Monitor CSV는 git에 안 올라감(`training/artifacts/`가 통째로 gitignore) — 실험 ID별로 `training/artifacts/<experiment-id>/`에 로컬 보관. `benchmarks/evaluations/*.json`(구조화된 평가 결과), 대표 replay JSON, 분석 스크립트는 git에 올라감 — 모델 없이도 실험 조건과 결론을 검토할 수 있게.

## 확인된 것

- `check_env` 통과, 시드 재현성 확인(지형·start/goal·random controller baseline 전부 포함).
- `baseline-axis-cap-fmax5`(축별 action clip 버그가 있던 상태): D20 55% 성공, 40% 맵 이탈. 현재 protocol에서는 superseded 상태다.
- `baseline-l2cap-fmax5`(L2-norm clip 수정 후): D20 65% 성공/20% 이탈. **V100 100 에피소드**: PPO 64% 성공, direct-to-goal 33%, random 0%.
- **지형 난이도 분석 결과, `baseline-l2cap-fmax5`에서는 가파른 셀이 0%** — 100개 지형 최대 경사가 14.3°인데 `theta_max=27.0°`라 사실상 평면 navigation이었음이 드러났다.
- `explore-fmax2-talus015-steps500`: 지형 결합 후보를 찾기 위해 `F_MAX=2.0`, `TALUS_ANGLE=0.15`를 함께 적용한 탐색 실행. 직선 경로 56% 차단, 100% 우회 가능이었지만 D20 성공 30%, 시간초과 55%였다. 두 변수를 함께 바꿨으므로 정식 단일 변수 실험이 아니다.
- `env-maxsteps1000-fmax2`: 위 탐색 조건에서 `MAX_STEPS`만 1000으로 늘린 실행. 시간초과는 5%로 줄었지만 맵 이탈 75%, 성공 20%였다.
- 세부 수치는 `benchmarks/evaluations/*.json` 참고.

## 아직 없음

현재 직진 가속·제동 실패 trajectory를 정식 experiment 문서로 기록 → reward/제어 가설을 단일 변수로 검증 → 필요성이 입증된 경우에만 observation 변경 → Phase 2c 성능 벤치마크.
