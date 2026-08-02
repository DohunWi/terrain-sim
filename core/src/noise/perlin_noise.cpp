#include <iostream>
#include <numbers>
#include <cmath>
#include <random>
#include "perlin_noise.h"

PerlinNoise::PerlinNoise()
    : gx_(std::make_unique<float[]>(TABLE_SIZE * TABLE_SIZE)),
      gy_(std::make_unique<float[]>(TABLE_SIZE * TABLE_SIZE))
{
}

void PerlinNoise::reseed(unsigned seed){
    std::mt19937 rng(seed);
    double pi = std::numbers::pi;            // double 버전 (기본)
    std::uniform_real_distribution<float> dist(0.0f, 2*pi);
    for(int j=0; j<TABLE_SIZE; j++){
        for(int i=0; i<TABLE_SIZE; i++){
            gx_[j*TABLE_SIZE + i] = cos(dist(rng));
            gy_[j*TABLE_SIZE + i] = sin(dist(rng));
        }
    }
}

static float fade(float t){
    float result;
    float t2 = t*t;
    float t3 = t2*t;
    float t5 = t2*t3;
    result = 6*t5 - 15*t2*t2 + 10*t3;

    return result;
}

static float lerp(float a, float b, float t){
    float result;
    result = a*(1-t) + b*t;
    return result;
}

float PerlinNoise::noise2D(float x, float y) const{
    float x0 = floor(x);
    float x1 = x0 + 1;
    float tx = x - x0;
    float fx = fade(tx);

    float y0 = floor(y);
    float y1 = y0 + 1;
    float ty = y - y0;
    float fy = fade(ty);

    float dx0 = x - x0;
    float dx1 = x - x1;
    float dy0 = y - y0;
    float dy1 = y - y1;

    float inf_x0_y0 = dx0 * gx_[(int)y0*TABLE_SIZE + (int)x0] + dy0 * gy_[(int)y0*TABLE_SIZE + (int)x0];
    float inf_x0_y1 = dx0 * gx_[(int)y1*TABLE_SIZE + (int)x0] + dy1 * gy_[(int)y1*TABLE_SIZE + (int)x0];
    float inf_x1_y0 = dx1 * gx_[(int)y0*TABLE_SIZE + (int)x1] + dy0 * gy_[(int)y0*TABLE_SIZE + (int)x1];
    float inf_x1_y1 = dx1 * gx_[(int)y1*TABLE_SIZE + (int)x1] + dy1 * gy_[(int)y1*TABLE_SIZE + (int)x1];

    float bottom;
    float top;
    float noise;
    bottom= lerp(inf_x0_y0, inf_x1_y0, fx);
    top= lerp(inf_x0_y1, inf_x1_y1, fx);
    noise = lerp(bottom, top, fy);
    return noise;
}
float PerlinNoise::fbm(float x, float y, int octaves, float persistence, float lacunarity) const{
    float sum=0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    for (int i=0; i<octaves; i++){
        float _i = PerlinNoise::noise2D(x*frequency, y*frequency)*amplitude;
        frequency *= lacunarity;
        amplitude *= persistence;

        sum += _i;
    }
    return sum;
}
