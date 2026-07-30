#pragma once
#include "vec3.h"
#include "../heightmap.h"

struct RigidBody{
    Vec3 position;
    Vec3 velocity;
    float mass;
};

void stepRigidBody(RigidBody& body, const Heightmap& terrain, const Vec3& gravity, const Vec3& force, float dt);