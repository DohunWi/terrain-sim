#pragma once
#include <string>
#include "heightmap.h"

void writePGM(const Heightmap& h, const std::string& filename);

// minVal/maxVal을 호출자가 직접 지정하는 버전 -- 두 heightmap을 같은 기준으로
// 정규화해서 비교해야 할 때 씀 (각자 자기 min/max로 스트레칭하면 실제로
// relief가 줄었어도 항상 0~255 꽉 채운 대비로 나와서 비교가 안 됨).
void writePGM(const Heightmap& h, const std::string& filename, float minVal, float maxVal);
