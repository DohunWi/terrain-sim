#pragma once

#include <memory>

class PerlinNoise {
  public:
      explicit PerlinNoise(unsigned seed);   // 생성자에서 테이블을 채움 — "생성되는 순간 항상 유효한 상태"
      float noise2D(float x, float y) const;
      float fbm(float x, float y, int octaves, float persistence, float lacunarity) const;

  private:
      static constexpr int TABLE_SIZE = 256;
      std::unique_ptr<float[]> gx_;
      std::unique_ptr<float[]> gy_;
  };