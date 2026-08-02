#pragma once
#include <array>

#include "heightmap.h"

struct LowestNeighbor {
    int x;
    int y;
    float value;
    float diff;
};  
struct NeighborList {
      std::array<LowestNeighbor, 4> items;
      int count = 0;
};
NeighborList findLowestNeighbor(const Heightmap& h, int x, int y);
void thermalErode(Heightmap& height, float talusAngle, float erosionRate, int iterations);