#pragma once
#include "heightmap.h"
#include <array>

void thermalErodePullModel(Heightmap& height, float talusAngle, float erosionRate, int iterations);