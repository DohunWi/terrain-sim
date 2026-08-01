#include <gtest/gtest.h>

#include "heightmap.h"
#include "noise/perlin_noise.h"
#include "physics/rigid_body.h"
#include <cmath>

// 실제 fbm 지형 위에서 stepRigidBody()를 검증한다. src/physics_test.cpp의
// 수동 확인(눈으로 KE/PE 출력 읽기)을 자동화된 assertion으로 옮긴 것.
namespace {

Heightmap makeTestTerrain(int seed) {
    const int width = 64;
    const int height = 64;
    PerlinNoise noise(seed);
    Heightmap terrain(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            terrain.at(x, y) = noise.fbm(static_cast<float>(x) / 10.0f,
                                          static_cast<float>(y) / 10.0f,
                                          3, 0.5f, 2.0f);
        }
    }
    return terrain;
}

Heightmap makeSlopeTerrain(float thetaRadians){
    const int width = 64;
    const int height = 64;

    Heightmap terrain(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            terrain.at(x, y) = std::tan(thetaRadians)*x;
        }
    }
    return terrain;

}
}  // namespace

// 에너지 보존: erosion의 mass-conservation 체크에 해당하는 물리 버전.
// 자유낙하 중엔 KE+PE가 semi-implicit Euler의 적분 오차만큼만 흔들려야
// 하고, 지형과 충돌해 법선 성분을 버리는 순간엔 KE가 줄어드는 게 정상
// (비탄성 충돌 모델)이라 총 에너지가 초기값보다 늘어나는 일은 절대
// 없어야 한다. 늘어난다면 적분/충돌 로직에 실제 버그가 있다는 뜻.
TEST(RigidBodyPhysics, EnergyNeverIncreasesDuringFreeFallAndLanding) {
    Heightmap terrain = makeTestTerrain(42);
    Vec3 gravity{0.0f, -9.8f, 0.0f};
    Vec3 noForce{0.0f, 0.0f, 0.0f};
    float dt = 1.0f / 60.0f;
    const float g = 9.8f;

    RigidBody body;
    body.position = Vec3{20.0f, 5.0f, 20.0f};
    body.velocity = Vec3{0.0f, 0.0f, 0.0f};
    body.mass = 1.0f;

    double initialEnergy = 0.0;
    double maxEnergy = -1e18;
    const double tolerance = 1e-2;  // semi-implicit Euler 적분 오차 허용치

    for (int i = 0; i < 300; ++i) {
        stepRigidBody(body, terrain, gravity, noForce, dt);
        double ke = 0.5 * body.mass * dot(body.velocity, body.velocity);
        double pe = body.mass * g * body.position.y;
        double totalEnergy = ke + pe;
        if (i == 0) initialEnergy = totalEnergy;
        maxEnergy = std::max(maxEnergy, totalEnergy);
    }

    EXPECT_LE(maxEnergy, initialEnergy + tolerance)
        << "total energy rose above its initial value during a no-force drop; "
           "integration or collision response likely leaks energy";
}

// 경사 임계각: F_max·cosθ ≥ m·g·sinθ ⟺ tanθ ≤ F_max/(m·g) ⟺ θ ≤ θ_max = atan(F_max/(m·g)).
// 에이전트는 수평으로만 밀 수 있어(경사면을 따라 미는 게 아니라) 경사가
// 가팔라질수록 밀어올리는 힘의 유효 성분(cosθ)이 줄어든다. F_max=5.0,
// m=1.0, g=9.8이면 θ_max≈27°. θ_easy=20°/θ_hard=35°로 θ_max에서 충분히
// 떨어뜨려 부동소수점/적분 오차로 인한 flaky한 경계 판정을 피한다.
TEST(RigidBodyPhysics, ClimbsSlopeBelowCriticalAngle) {
    const float thetaEasy = 20.0f*M_PI/180.0f;
    Heightmap terrain = makeSlopeTerrain(thetaEasy);

    Vec3 gravity{0.0f, -9.8f, 0.0f};
    float dt = 1.0f / 60.0f;
    const float F_max = 5.0f;

    RigidBody body;
    body.position.x = 10.0f;
    body.position.z = 32.0f;
    body.position.y = terrain.sample(body.position.x, body.position.z).height;
    body.velocity = Vec3{0.0f, 0.0f, 0.0f};
    body.mass = 1.0f;

    Vec3 force{F_max, 0.0f, 0.0f};  // 이 지형은 gradZ=0, gradX>0(오르막=+x)라 그냥 +x로 밀면 됨

    float startX = body.position.x;
    for (int i = 0; i < 300; ++i) {
        stepRigidBody(body, terrain, gravity, force, dt);
    }

    EXPECT_GT(body.position.x, startX)
        << "body failed to climb a slope below the critical angle";
}

TEST(RigidBodyPhysics, CannotClimbSlopeAboveCriticalAngle) {
      const float thetaHard = 35.0f*M_PI/180.0f;
      Heightmap terrain = makeSlopeTerrain(thetaHard);

      Vec3 gravity{0.0f, -9.8f, 0.0f};
      float dt = 1.0f / 60.0f;
      const float F_max = 5.0f;

      RigidBody body;
      body.position.x = 10.0f;
      body.position.z = 32.0f;
      body.position.y = terrain.sample(body.position.x, body.position.z).height;
      body.velocity = Vec3{0.0f, 0.0f, 0.0f};
      body.mass = 1.0f;

      Vec3 force{F_max, 0.0f, 0.0f};  // 이 지형은 gradZ=0, gradX>0(오르막=+x)라 그냥 +x로 밀면 됨

      float startX = body.position.x;
      for (int i = 0; i < 300; ++i) {
          stepRigidBody(body, terrain, gravity, force, dt);
      }

      EXPECT_LE(body.position.x, startX)
          << "body climbed a slope above the critical angle";
}
