#pragma once
#include "heightmap.h"

struct LowestNeighbor {
    int x;
    int y;
    float value;
    float diff;
};  

std::vector<LowestNeighbor> findLowestNeighbor(const Heightmap& h, int x, int y);
void thermalErode(Heightmap& height, float talusAngle, float erosionRate, int iterations);