include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(physics_tests
    physics_test.cpp
    erosion_test.cpp
    erosion_reference.cpp
    ../src/heightmap.cpp
    ../src/noise/perlin_noise.cpp
    ../src/physics/rigid_body.cpp
    ../src/physics/vec3.cpp
    ../src/erosion/thermal_erosion.cpp
    ../src/erosion/thermal_erosion_pull.cpp
)
target_include_directories(physics_tests PRIVATE ../src)
target_link_libraries(physics_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(physics_tests)
