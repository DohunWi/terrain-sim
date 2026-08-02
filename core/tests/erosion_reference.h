#pragma once
#include "../src/heightmap.h"

// Frozen copy of thermalErode/findLowestNeighbor exactly as they were before
// EXP-008's std::vector -> std::array + buffer-reuse refactor (commit before
// this file was added). Test-only: exists purely so erosion_test.cpp can
// diff the refactored core/src/erosion/thermal_erosion.cpp against the
// pre-refactor behavior, cell-by-cell, for the same input. Never included
// by production code.
struct LowestNeighborRef {
    int x;
    int y;
    float value;
    float diff;
};

std::vector<LowestNeighborRef> findLowestNeighborRef(const Heightmap& h, int x, int y);
void thermalErodeRef(Heightmap& height, float talusAngle, float erosionRate, int iterations);
