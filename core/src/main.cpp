#include <iostream>

#include "heightmap.h"
#include "./erosion/thermal_erosion.h"
#include "pgm.h"
#include "./noise/perlin_noise.h"

int main() {
    std::cout << "terrain-sim core: build OK\n";
    PerlinNoise noise(42);
    Heightmap h(64, 64); 
    
    float scale = 10.0f;
    float total = 0.0f;

    for (int y = 0; y < h.height(); ++y) {
        for (int x = 0; x < h.width(); ++x) {
            h.at(x,y) = noise.fbm((float)x/scale, (float)y/scale, 3, 0.5, 2);
            std::cout << h.at(x, y) << " ";
            total += h.at(x,y);
        }
        std::cout << "\n";
    }
    writePGM(h, "before.pgm");
    std::cout << "before erosion total :" << total << "\n";

    total = 0;
    thermalErode(h, 0.1f, 0.3f, 10);
    for (int y = 0; y < h.height(); ++y) {
        for (int x = 0; x < h.width(); ++x) {
            std::cout << h.at(x, y) << " ";
            total += h.at(x,y);
        }
        std::cout << "\n";
    }
    writePGM(h, "after.pgm");
    std::cout << "after erosion total :" << total << "\n";
    return 0;
}
