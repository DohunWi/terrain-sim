#pragma once

struct Vec3{
    float x;
    float y;
    float z;
    Vec3 operator+(const Vec3& other) const {
        return Vec3{ x+other.x, y+other.y, z+other.z};
    }

    Vec3 operator*(float s) const {
        return Vec3{x*s, y*s, z*s};
    }
};