cmake_minimum_required(VERSION 3.20)
project(terrain_sim_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(terrain_sim_core
    src/main.cpp
    src/heightmap.cpp
    src/erosion/thermal_erosion.cpp
    src/noise/perlin_noise.cpp
    src/pgm.cpp
    src/erosion/droplet_erosion.cpp
    src/net/socket.cpp
    src/net/tcp_server.cpp
    src/net/io.cpp
    src/net/protocol.cpp
    src/net/params.cpp
    src/physics/rigid_body.cpp
    src/physics/vec3.cpp
)

target_include_directories(terrain_sim_core PRIVATE src)

add_executable(tune_cli
    src/tune_cli.cpp
    src/heightmap.cpp
    src/erosion/droplet_erosion.cpp
    src/erosion/thermal_erosion.cpp
    src/noise/perlin_noise.cpp
    src/pgm.cpp
)

target_include_directories(tune_cli PRIVATE src)

add_executable(physics_test
    src/physics_test.cpp
    src/heightmap.cpp
    src/noise/perlin_noise.cpp
    src/physics/rigid_body.cpp
    src/physics/vec3.cpp
)

target_include_directories(physics_test PRIVATE src)

enable_testing()
add_subdirectory(tests)

# --- Phase 2b: pybind11 바인딩 (training/ RL 루프용, 선택적 타겟) ---
# pyenv shim이 PATH에서 conda보다 우선하는 이 머신 환경에서는 plain
# find_package(Python3)가 pybind11/gymnasium/stable-baselines3가 깔린
# conda 인터프리터를 못 찾는다 — 활성 conda env를 가리키는 CONDA_PREFIX를
# Python3_ROOT_DIR 힌트로 먼저 준다. pyenv를 쓰지 않는 환경에서도 이 블록은
# CONDA_PREFIX가 없으면 그냥 기본 탐색으로 넘어가므로 안전하다.
if(DEFINED ENV{CONDA_PREFIX} AND NOT DEFINED Python3_ROOT_DIR)
    set(Python3_ROOT_DIR "$ENV{CONDA_PREFIX}")
endif()
find_package(Python3 COMPONENTS Interpreter Development QUIET)

if(Python3_FOUND)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
        OUTPUT_VARIABLE pybind11_CMAKE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE pybind11_FIND_RESULT
        ERROR_QUIET
    )
endif()

if(Python3_FOUND AND pybind11_FIND_RESULT EQUAL 0)
    list(APPEND CMAKE_PREFIX_PATH "${pybind11_CMAKE_DIR}")
    find_package(pybind11 CONFIG QUIET)
endif()

if(pybind11_FOUND)
    pybind11_add_module(terrain_sim_py
        src/bindings/py_bindings.cpp
        src/heightmap.cpp
        src/noise/perlin_noise.cpp
        src/erosion/thermal_erosion.cpp
        src/erosion/droplet_erosion.cpp
        src/physics/rigid_body.cpp
        src/physics/vec3.cpp
    )
    target_include_directories(terrain_sim_py PRIVATE src)
    set_target_properties(terrain_sim_py PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    )
    message(STATUS "pybind11 found (${Python3_EXECUTABLE}) — terrain_sim_py target enabled, output in build/python/")
else()
    message(STATUS "pybind11 not found for ${Python3_EXECUTABLE} — skipping terrain_sim_py (Phase 2b RL binding). "
                    "Install with: <python for training/> -m pip install pybind11")
endif()
