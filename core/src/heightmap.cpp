#include "heightmap.h"

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
