#include "heightmap.h"
#include <cmath>
#include <algorithm>

namespace {
float lerp(float a, float b, float t) {
    return a * (1 - t) + b * t;
}
}  // namespace

Heightmap::Heightmap(int width, int height)
    : width_(width),
      height_(height),
      data_(static_cast<size_t>(width) * height, 0.0f)
      // vector 생성자에 (개수, 초기값)을 줘서 width*height 칸을 0.0f로 채운다.
      // C였으면 calloc(width*height, sizeof(float)) 한 줄에 대응.
{
}

float& Heightmap::at(int x, int y) {
    return data_[static_cast<size_t>(y) * width_ + x];
}

float Heightmap::at(int x, int y) const {
    return data_[static_cast<size_t>(y) * width_ + x];
}

HeightSample Heightmap::sample(float x, float y) const {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    x0 = std::clamp(x0, 0, width_ - 2);
    y0 = std::clamp(y0, 0, height_ - 2);
    x = std::clamp(x, 0.0f, float(width_ - 1));
    y = std::clamp(y, 0.0f, float(height_ - 1));

    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float tx = x - x0;
    float ty = y - y0;

    float h00 = at(x0, y0);
    float h10 = at(x1, y0);
    float h01 = at(x0, y1);
    float h11 = at(x1, y1);

    float bottom = lerp(h00, h10, tx);
    float top = lerp(h01, h11, tx);

    HeightSample hs;
    hs.tx = tx;
    hs.ty = ty;
    hs.height = lerp(bottom, top, ty);
    hs.gradX = lerp(h10 - h00, h11 - h01, ty);
    hs.gradY = lerp(h01 - h00, h11 - h10, tx);

    return hs;
}
