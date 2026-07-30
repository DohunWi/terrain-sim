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
)

target_include_directories(physics_test PRIVATE src)

# GoogleTest 연동은 Phase 3에서 추가 예정 (core/tests/)
# enable_testing()
# add_subdirectory(tests)
