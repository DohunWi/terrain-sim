#pragma once

struct Vec3{
    float x;
    float y;
    float z;
    Vec3 operator+(const Vec3& other) const {
        return Vec3{ x+other.x, y+other.y, z+other.z};
    }
    Vec3 operator-(const Vec3& other) const {
        return Vec3{ x-other.x, y-other.y, z-other.z};
    }
    Vec3 operator*(float s) const {
        return Vec3{x*s, y*s, z*s};
    }
};
float length(Vec3 v);
float dot(Vec3 v1, Vec3 v2);
Vec3 normalize(Vec3 v1);