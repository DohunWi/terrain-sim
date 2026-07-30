#include "rigid_body.h"

void stepRigidBody(RigidBody& body, const Heightmap& terrain, const Vec3& gravity, const Vec3& force, float dt){

    Vec3 acceleration = gravity + force * (1.0f / body.mass);
    body.velocity = body.velocity + acceleration * dt;
    body.position = body.position + body.velocity * dt;
    float groundHeight = terrain.sample(body.position.x, body.position.z).height;

    if(body.position.y < groundHeight){
        body.position.y = groundHeight;
        body.velocity = Vec3{0,0,0};
    }
    
}