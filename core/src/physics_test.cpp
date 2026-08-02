#include <algorithm>
#include <iostream>

#include "heightmap.h"
#include "noise/perlin_noise.h"
#include "physics/rigid_body.h"

// 진짜 펄린 노이즈 지형 위에서 stepRigidBody()를 검증하는 임시 테스트
// 하네스. practice/freefall.cpp이 상수 바닥으로 확인했던 걸, 굴곡진
// 실제 Heightmap으로 다시 확인한다.
int main() {
    const int width = 64;
    const int height = 64;

    PerlinNoise noise;
    noise.reseed(42);
    Heightmap terrain(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            terrain.at(x, y) = noise.fbm(static_cast<float>(x) / 10.0f,
                                          static_cast<float>(y) / 10.0f,
                                          3, 0.5f, 2.0f);
        }
    }

    Vec3 gravity{0.0f, -9.8f, 0.0f};
    float dt = 1.0f / 60.0f;
    Vec3 noForce{0.0f, 0.0f, 0.0f};

    RigidBody body;
    body.position = Vec3{32.0f, 10.0f, 32.0f};  // 지형 내부의 한 점, 지형보다 훨씬 위
    body.velocity = Vec3{0.0f, 0.0f, 0.0f};
    body.mass = 1.0f;

    float groundHeight = terrain.sample(body.position.x, body.position.z).height;
    std::cout << "terrain height at (32, 32): " << groundHeight << "\n";
    std::cout << "--- test 1: straight fall (no force) ---\n";
    for (int i = 0; i < 180; ++i) {
        stepRigidBody(body, terrain, gravity, noForce, dt);
        std::cout << i << ". position y: " << body.position.y << "\n";
    }

    // 착지 전까지만 관찰 — 착지 후 수평 속도가 리셋되는 건 이번 단계 스코프 밖.
    std::cout << "\n--- test 2: gravity + sideways force (airborne only) ---\n";
    RigidBody projectile;
    projectile.position = Vec3{10.0f, 8.0f, 32.0f};
    projectile.velocity = Vec3{0.0f, 0.0f, 0.0f};
    projectile.mass = 1.0f;
    Vec3 pushForce{5.0f, 0.0f, 0.0f};

    for (int i = 0; i < 60; ++i) {
        stepRigidBody(projectile, terrain, gravity, pushForce, dt);
        std::cout << i << ". x=" << projectile.position.x << " y=" << projectile.position.y << "\n";
    }

    // 에너지 보존 검증: erosion의 mass-conservation 체크에 해당하는 물리 버전.
    // 자유낙하 중엔 KE+PE가 거의 일정해야 하고 (semi-implicit Euler의 적분
    // 오차만큼만 흔들려야 함), 지형과 충돌해 법선 성분을 버리는 순간엔
    // KE가 줄어드는 게 정상(비탄성 충돌 모델)이라 총 에너지가 늘어나는 일은
    // 절대 없어야 한다. 늘어난다면 적분/충돌 로직에 실제 버그가 있다는 뜻.
    std::cout << "\n--- test 3: energy conservation ---\n";
    RigidBody dropTest;
    dropTest.position = Vec3{20.0f, 5.0f, 20.0f};
    dropTest.velocity = Vec3{0.0f, 0.0f, 0.0f};
    dropTest.mass = 1.0f;

    const float g = 9.8f;  // gravity 벡터의 크기, PE = m*g*h 계산용
    double initialEnergy = 0.0;
    double maxEnergy = -1e18;
    double minEnergy = 1e18;

    for (int i = 0; i < 300; ++i) {
        stepRigidBody(dropTest, terrain, gravity, noForce, dt);
        double ke = 0.5 * dropTest.mass * dot(dropTest.velocity, dropTest.velocity);
        double pe = dropTest.mass * g * dropTest.position.y;
        double totalEnergy = ke + pe;
        if (i == 0) initialEnergy = totalEnergy;
        maxEnergy = std::max(maxEnergy, totalEnergy);
        minEnergy = std::min(minEnergy, totalEnergy);
        std::cout << i << ". KE=" << ke << " PE=" << pe << " total=" << totalEnergy << "\n";
    }

    std::cout << "\ninitial total energy: " << initialEnergy
               << ", max: " << maxEnergy
               << ", min: " << minEnergy << "\n";
    std::cout << "max increase above initial: " << (maxEnergy - initialEnergy) << "\n";

    return 0;
}
