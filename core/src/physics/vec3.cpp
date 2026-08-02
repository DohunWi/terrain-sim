#include "vec3.h"
#include <cmath>
#include <iostream>
float length(Vec3 v){
    return sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
}
float dot(Vec3 v1, Vec3 v2){
    return (v1.x*v2.x + v1.y*v2.y + v1.z*v2.z);
}
Vec3 normalize(Vec3 v1){
    return v1*(1.0f/length(v1));
}