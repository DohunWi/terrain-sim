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
    PerlinNoise noise;
    noise.reseed(seed);
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

// 폭 1그리드유닛짜리 벽(한 열만 솟은 지형). CCD(연속 충돌 감지) 부재
// 테스트용 — stepRigidBody()가 스텝 시작 시점 위치 한 곳에서만 지형을
// 샘플하고 시작점~끝점 사이 궤적은 검사하지 않는다는 구조적 성질을
// 검증한다.
Heightmap makeWallTerrain(int wallColumn, float wallHeight) {
    const int width = 64;
    const int height = 64;
    Heightmap terrain(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            terrain.at(x, y) = (x == wallColumn) ? wallHeight : 0.0f;
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

// 결정성: stepRigidBody()는 랜덤성이 없는 순수 계산이라, 같은 초기 상태·
// 같은 지형·같은 입력을 두 개의 독립된 RigidBody에 넣고 나란히 굴리면
// 매 스텝 완전히 같은 결과가 나와야 한다(부동소수점이라도 근사치가 아니라
// 정확히 일치 — 같은 코드를 같은 하드웨어에서 두 번 도는 것뿐이므로).
// RL 학습의 고정 시드 재현성이 이 성질 위에 있다.
TEST(RigidBodyPhysics, StepIsDeterministicForIdenticalInputs) {
    int seed = 42;
    Heightmap terrain = makeTestTerrain(seed);
    Vec3 gravity{0.0f, -9.8f, 0.0f};
    Vec3 noForce{0.0f, 0.0f, 0.0f};
    float dt = 1.0f / 60.0f;

    RigidBody body1;
    body1.position = Vec3{20.0f, 5.0f, 20.0f};
    body1.velocity = Vec3{0.0f, 0.0f, 0.0f};
    body1.mass = 1.0f;

    RigidBody body2;
    body2.position = Vec3{20.0f, 5.0f, 20.0f};
    body2.velocity = Vec3{0.0f, 0.0f, 0.0f};
    body2.mass = 1.0f;


    for (int i = 0; i < 300; ++i) {
        stepRigidBody(body1, terrain, gravity, noForce, dt);
        stepRigidBody(body2, terrain, gravity, noForce, dt);

        EXPECT_EQ(body1.position, body2.position)
            << "rigid bodies' positions diverged despite identical inputs";

        EXPECT_EQ(body1.velocity, body2.velocity)
            << "rigid bodies' velocities diverged despite identical inputs";
    }

}

// 경계 조건: 에이전트가 맵 밖으로 나가면 stepRigidBody()가 Heightmap::sample()을
// grid 범위 밖 좌표로 호출한다. 이때 크래시 없이, 가장자리 값으로 clamp된
// 높이가 나와야 한다 - x=width-1(정상 clamp되는 격자 인덱스라 버그와
// 무관하게 항상 신뢰 가능한 기준값)과 x=1000(범위 밖) 샘플이 같아야 함.
// 터널링: 한 스텝의 수평 이동 거리(velocity.x * dt = 6유닛)가 벽 폭(1
// 그리드유닛)보다 훨씬 크면, 몸체가 벽의 존재를 한 번도 감지하지 못한 채
// 반대편에 착지한다 — 속도(velocity.x)도 충돌 없이 전혀 안 바뀐 채로.
// velocity를 힘 입력으로 "도달시키지" 않고 직접 세팅한 이유: 이 속도가
// 현재 F_MAX/마찰(없음) 등 물리 파라미터로 실제 도달 가능한지는 여기서
// 안 다룬다 — stepRigidBody() 자체가 이런 입력에 안전한지만 본다. 이렇게
// 분리해두면 나중에 마찰 등이 추가돼 도달 가능 속도가 달라져도 이 테스트가
// 검증하는 성질 자체는 그대로 유효하다.
TEST(RigidBodyPhysics, TunnelsThroughNarrowWallAtHighVelocity) {
    const int wallColumn = 35;
    Heightmap terrain = makeWallTerrain(wallColumn, 10.0f);
    Vec3 gravity{0.0f, -9.8f, 0.0f};
    Vec3 noForce{0.0f, 0.0f, 0.0f};
    float dt = 1.0f / 60.0f;

    RigidBody body;
    body.position = Vec3{30.0f, 1.0f, 32.0f};  // 벽에서 5칸 떨어진, 벽 높이(10)보다 훨씬 낮은 고도에서 공중
    body.velocity = Vec3{360.0f, 0.0f, 0.0f};  // 한 스텝에 6유닛 이동 = 벽을 건너뜀
    body.mass = 1.0f;

    stepRigidBody(body, terrain, gravity, noForce, dt);

    EXPECT_GT(body.position.x, float(wallColumn))
        << "body did not end up past the wall it should have tunneled through";
    EXPECT_NEAR(body.velocity.x, 360.0f, 1e-3f)
        << "horizontal velocity changed, meaning a collision was actually detected -- "
           "the wall may no longer be tall/thin enough to reproduce the gap";
}

// 대조군: 같은 벽이라도 연속된 힘으로 서서히 접근하면 정상적으로 막힌다 —
// 벽 자체가 무력한 게 아니라, 고속에서만 나타나는 이산 샘플링의 구조적
// 결함임을 보여준다. 벽 근처에서 gradX가 10(경사각 ~84°)까지 치솟아
// CannotClimbSlopeAboveCriticalAngle과 같은 이유로 F_max=5.0로는 못 넘는다.
TEST(RigidBodyPhysics, BlocksApproachToNarrowWallAtModerateVelocity) {
    const int wallColumn = 35;
    Heightmap terrain = makeWallTerrain(wallColumn, 10.0f);
    Vec3 gravity{0.0f, -9.8f, 0.0f};
    float dt = 1.0f / 60.0f;
    const float F_max = 5.0f;

    RigidBody body;
    body.position.x = 25.0f;
    body.position.z = 32.0f;
    body.position.y = terrain.sample(body.position.x, body.position.z).height;
    body.velocity = Vec3{0.0f, 0.0f, 0.0f};
    body.mass = 1.0f;

    Vec3 force{F_max, 0.0f, 0.0f};

    for (int i = 0; i < 300; ++i) {
        stepRigidBody(body, terrain, gravity, force, dt);
    }

    EXPECT_LT(body.position.x, float(wallColumn))
        << "body tunneled through the wall even at a moderate, continuously-applied approach speed";
}

TEST(HeightmapSample, ClampsQueryFarOutsideGrid) {
    Heightmap terrain = makeTestTerrain(42);
    int width = terrain.width();
    float x = float(width-1);
    float z = 32.5f;
    float edgeHeight = terrain.sample(x,z).height;
    float farHeight = terrain.sample(1000.5f,z).height;

    EXPECT_EQ(farHeight, edgeHeight)
        << "sample() extrapolated instead of clamping for a query far outside the grid";
}
