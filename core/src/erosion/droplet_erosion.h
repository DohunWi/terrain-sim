#pragma once
#include "heightmap.h"

struct ErosionParams{
    float inertia;
    float minSlope;
    float capacityFactor;
    float erosionFactor;
    float depositFactor;
    float gravity;
    float evaporateRate;
    float waterThreshold;
    int maxLifeTime;
};

// 리턴값: 물방울이 수명이 다 하거나 맵 밖으로 나가면서 지형에 못 돌려놓고
// 그대로 들고 사라진 sediment 총량. thermalErode와 달리 droplet erosion은
// 이 양만큼 Σh(before) != Σh(after)가 나는 게 정상이라, 호출자가 mass
// conservation을 검증하려면 이 값을 같이 더해서 확인해야 함.
double dropletErode(Heightmap& height, const ErosionParams& params, int numDroplets, unsigned seed);