#include "rigid_body.h"

void stepRigidBody(RigidBody& body, const Heightmap& terrain, const Vec3& gravity, const Vec3& force, float dt){

    Vec3 acceleration = gravity + force * (1.0f / body.mass);
    HeightSample hs = terrain.sample(body.position.x, body.position.z);
    float groundHeight = hs.height;
    float gradX = hs.gradX;
    float gradZ = hs.gradY;
    Vec3 normal = normalize(Vec3{-gradX, 1, -gradZ});

    if(body.position.y > groundHeight){
            body.velocity = body.velocity + acceleration * dt;
            body.position = body.position + body.velocity * dt;
        }
    else{
        acceleration = acceleration - normal*dot(acceleration, normal);
        body.velocity = body.velocity + acceleration*dt;
        Vec3 v_normal = normal*dot(body.velocity, normal);
        Vec3 v_tangential = body.velocity - v_normal; 
        body.position.y = groundHeight;
        body.velocity = v_tangential;
        body.position = body.position + body.velocity*dt;
    };
    
}