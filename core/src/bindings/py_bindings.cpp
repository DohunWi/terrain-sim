// pybind11 바인딩 — C++ 시뮬레이션 코어를 Python(RL 학습 루프)에 그대로 노출한다.
// 여기엔 새 알고리즘이 없다: 기존 core/ 클래스/함수를 감싸는 wiring뿐.
// reset()/step() 같은 실제 RL task 로직(reward, observation, 종료 조건)은
// training/의 Gymnasium 래퍼에 둔다 — 여긴 그 래퍼가 부를 primitive만 제공.

#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>

#include "../erosion/droplet_erosion.h"
#include "../erosion/thermal_erosion.h"
#include "../heightmap.h"
#include "../noise/perlin_noise.h"
#include "../physics/rigid_body.h"
#include "../physics/vec3.h"

namespace py = pybind11;

namespace {

// main.cpp/tune_cli.cpp가 각각 인라인으로 하던 "fbm으로 heightmap 채우기" 루프를
// 재사용 가능한 형태로 한 번 더 감싼 것. 알고리즘은 기존 PerlinNoise::fbm 그대로.
Heightmap generateFbmHeightmap(int width, int height, unsigned seed, float scale,
                                int octaves, float persistence, float lacunarity) {
    Heightmap hm(width, height);
    PerlinNoise noise;
    noise.reseed(seed);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            hm.at(x, y) = noise.fbm(static_cast<float>(x) / scale,
                                     static_cast<float>(y) / scale, octaves,
                                     persistence, lacunarity);
        }
    }
    return hm;
}

// numpy로 복사해서 넘긴다 (zero-copy 아님 — Heightmap::data_는 private이라
// 버퍼 프로토콜을 걸려면 core/ 쪽에 접근자를 추가해야 함. 관측값 하나 만드는
// 정도로는 지금 충분히 빠르고, Phase 2c 성능 작업에서 병목으로 잡히면 그때
// 최적화 대상으로 삼는다 — "측정 먼저" 컨벤션).
py::array_t<float> heightmapToNumpy(const Heightmap& hm) {
    py::array_t<float> arr({hm.height(), hm.width()});
    auto buf = arr.mutable_unchecked<2>();
    for (int y = 0; y < hm.height(); ++y) {
        for (int x = 0; x < hm.width(); ++x) {
            buf(y, x) = hm.at(x, y);
        }
    }
    return arr;
}

}  // namespace

PYBIND11_MODULE(terrain_sim_py, m) {
    m.doc() = "terrain-sim C++ core bindings (in-process interface for the Gymnasium RL env)";

    py::class_<Vec3>(m, "Vec3")
        .def(py::init([](float x, float y, float z) { return Vec3{x, y, z}; }),
             py::arg("x") = 0.0f, py::arg("y") = 0.0f, py::arg("z") = 0.0f)
        .def_readwrite("x", &Vec3::x)
        .def_readwrite("y", &Vec3::y)
        .def_readwrite("z", &Vec3::z)
        .def(py::self + py::self)
        .def(py::self - py::self)
        .def(py::self * float())
        .def("__repr__", [](const Vec3& v) {
            return "Vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y) +
                   ", " + std::to_string(v.z) + ")";
        });

    m.def("length", &length, py::arg("v"));
    m.def("dot", &dot, py::arg("a"), py::arg("b"));
    m.def("normalize", &normalize, py::arg("v"));

    py::class_<HeightSample>(m, "HeightSample")
        .def_readonly("height", &HeightSample::height)
        .def_readonly("tx", &HeightSample::tx)
        .def_readonly("ty", &HeightSample::ty)
        .def_readonly("grad_x", &HeightSample::gradX)
        .def_readonly("grad_y", &HeightSample::gradY);

    py::class_<Heightmap>(m, "Heightmap")
        .def(py::init<int, int>(), py::arg("width"), py::arg("height"))
        .def("get", [](const Heightmap& hm, int x, int y) { return hm.at(x, y); },
             py::arg("x"), py::arg("y"))
        .def("set", [](Heightmap& hm, int x, int y, float v) { hm.at(x, y) = v; },
             py::arg("x"), py::arg("y"), py::arg("value"))
        .def("sample", &Heightmap::sample, py::arg("x"), py::arg("y"))
        .def_property_readonly("width", &Heightmap::width)
        .def_property_readonly("height", &Heightmap::height)
        .def("to_numpy", &heightmapToNumpy);

    m.def("generate_fbm_heightmap", &generateFbmHeightmap, py::arg("width"),
          py::arg("height"), py::arg("seed"), py::arg("scale"), py::arg("octaves"),
          py::arg("persistence"), py::arg("lacunarity"));

    m.def("thermal_erode", &thermalErode, py::arg("height"), py::arg("talus_angle"),
          py::arg("erosion_rate"), py::arg("iterations"));

    py::class_<ErosionParams>(m, "ErosionParams")
        .def(py::init<>())
        .def_readwrite("inertia", &ErosionParams::inertia)
        .def_readwrite("min_slope", &ErosionParams::minSlope)
        .def_readwrite("capacity_factor", &ErosionParams::capacityFactor)
        .def_readwrite("erosion_factor", &ErosionParams::erosionFactor)
        .def_readwrite("deposit_factor", &ErosionParams::depositFactor)
        .def_readwrite("gravity", &ErosionParams::gravity)
        .def_readwrite("evaporate_rate", &ErosionParams::evaporateRate)
        .def_readwrite("water_threshold", &ErosionParams::waterThreshold)
        .def_readwrite("max_life_time", &ErosionParams::maxLifeTime);

    m.def("droplet_erode", &dropletErode, py::arg("height"), py::arg("params"),
          py::arg("num_droplets"), py::arg("seed"));

    py::class_<RigidBody>(m, "RigidBody")
        .def(py::init<>())
        .def_readwrite("position", &RigidBody::position)
        .def_readwrite("velocity", &RigidBody::velocity)
        .def_readwrite("mass", &RigidBody::mass);

    m.def("step_rigid_body", &stepRigidBody, py::arg("body"), py::arg("terrain"),
          py::arg("gravity"), py::arg("force"), py::arg("dt"),
          "Advance one semi-implicit-Euler step with terrain collision + slope sliding.");
}
