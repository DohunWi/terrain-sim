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

    PerlinNoise noise(42);
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

    return 0;
}
