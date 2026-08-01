# RL 바인딩 (Phase 2b, pybind11)

`core/`(C++ 시뮬레이션 코어)를 Python RL 학습 루프(`training/`)에 인프로세스로 연결하는 계층. `CLAUDE.md`/스펙 §2에 적힌 대로 TCP 소켓(`core/src/net/`)이 아니라 pybind11 — 학습은 Unity 시각화와 달리 스텝당 처리량이 훨씬 중요해서, 소켓 왕복 비용을 감당할 이유가 없다.

## 무엇을 바인딩했나

`core/src/bindings/py_bindings.cpp` — 모듈 이름 `terrain_sim_py`. 새 알고리즘 없음, 기존 `core/` 클래스/함수를 그대로 노출하는 wiring만 있다

- `Heightmap`, `HeightSample`, `PerlinNoise`, `Vec3` + `length`/`dot`/`normalize`
- `generate_fbm_heightmap(...)` — `main.cpp`/`tune_cli.cpp`가 각자 인라인으로 하던 "fbm으로 heightmap 채우기" 루프를 재사용 가능한 형태로 한 번 더 감싼 것
- `thermal_erode`, `droplet_erode` + `ErosionParams` — 에피소드마다 지형을 재시드하는 domain randomization용 (로드맵 3번 항목)
- `RigidBody`, `step_rigid_body` — Phase 2a에서 만든 물리(중력+지형 충돌+법선 기반 슬로프 슬라이딩) 그대로

**여기 없는 것, 의도적으로**: `reset()`/`step(action)`의 실제 RL task 로직(관측값 구성, reward, 에피소드 종료 조건)은 이 바인딩이 아니라 `training/env.py`의 Gymnasium 래퍼에 있다. 바인딩은 순수 wiring, task 설계는 별도 계층 — task 설계 내용 자체는 `training/README.md` 참고.

## 왜 pybind11 CMake 타겟이 조건부(optional)인가

`core/CMakeLists.txt`에 `pybind11_add_module(terrain_sim_py ...)`을 추가하되, `find_package(pybind11 CONFIG QUIET)`가 실패하면 그냥 건너뛰고 나머지 타겟(`terrain_sim_core`, `tune_cli`, `physics_test`)은 평소대로 빌드되게 했다. 물리/침식/네트워크 핵심 빌드가 Python 환경 유무에 매달리게 만들 이유가 없다 — Phase 3 GoogleTest를 `enable_testing()`으로 조건부 처리해둔 것과 같은 이유.

## pyenv vs conda — 이 머신에서만 필요했던 우회

이 프로젝트는 원래 pyenv(`python3` 3.9.1)를 써왔는데(`tools/tuner_server.py`), pybind11/gymnasium/stable-baselines3는 **conda `base`**(3.13.5, `~/miniconda3`)에 설치했다. 문제는 이 머신에서 로그인 셸이어도 `pyenv shims`가 PATH에서 conda보다 먼저 온다는 것 (`CONDA_PREFIX`는 정상적으로 설정돼 있는데도): plain `find_package(Python3)`를 쓰면 CMake가 pybind11이 없는 pyenv 3.9.1을 집어서 바인딩 타겟이 조용히 스킵된다.

고친 방법: `CONDA_PREFIX` 환경변수가 있으면 CMake의 `Python3_ROOT_DIR` 힌트로 먼저 넣어준다 (`find_package(Python3)` 호출 전에). `Python3_ROOT_DIR`은 `FindPython3` 모듈이 다른 어떤 자동 탐색보다 우선시하는 공식 힌트라서, 이게 있으면 conda의 인터프리터를 확실히 잡는다. `CONDA_PREFIX`가 없는 환경(예: pyenv도 conda도 안 쓰는 CI 머신)에서는 이 블록이 아무 영향도 안 주고 기본 탐색으로 넘어간다.

## 빌드 & 사용

```bash
cd core
cmake -S . -B build          # "pybind11 found (...)" 로그로 확인
cmake --build build --target terrain_sim_py
```

빌드 결과물은 `core/build/python/terrain_sim_py.cpython-*.so`. `training/`에서 쓰려면:

```python
import sys
sys.path.insert(0, "../core/build/python")  # 또는 PYTHONPATH에 추가
import terrain_sim_py as ts
```

## 검증

빌드 후 conda base 인터프리터로 실제 import + 호출 확인 (스크래치 스크립트, 커밋 안 함): heightmap 생성 → numpy 변환 → `RigidBody`를 지형 위에 올리고 300스텝 적분 → 실제로 경사를 타고 슬라이딩(=Phase 2a 물리가 바인딩을 거쳐도 그대로 재현됨을 확인, 단순히 수직 낙하로 안 끝남) → thermal erosion 전후 질량 합 비교(diff ~1e-6, float 오차 수준) → droplet erosion 호출까지 전부 정상.
