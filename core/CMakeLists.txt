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
)

target_include_directories(terrain_sim_core PRIVATE src)

# GoogleTest 연동은 Phase 3에서 추가 예정 (core/tests/)
# enable_testing()
# add_subdirectory(tests)
