#pragma once

#include <memory>

class PerlinNoise {
  public:
      explicit PerlinNoise();
      float noise2D(float x, float y) const;
      float fbm(float x, float y, int octaves, float persistence, float lacunarity) const;
      void reseed(unsigned seed);
      static constexpr int TABLE_SIZE = 256;

  private:
      std::unique_ptr<float[]> gx_;
      std::unique_ptr<float[]> gy_;
  };