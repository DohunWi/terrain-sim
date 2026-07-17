#include <cmath>
#include <iostream>

#include "heightmap.h"
#include "./erosion/thermal_erosion.h"
#include "./erosion/droplet_erosion.h"
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

    // --- droplet(hydraulic) erosion demo: separate heightmap, so its before/after
    // isn't muddled by the thermal erosion pass above ---
    Heightmap h2(64, 64);
    for (int y = 0; y < h2.height(); ++y) {
        for (int x = 0; x < h2.width(); ++x) {
            h2.at(x, y) = noise.fbm((float)x / scale, (float)y / scale, 3, 0.5, 2);
        }
    }
    writePGM(h2, "droplet_before.pgm");

    double dropletInitialSum = 0.0;
    for (int y = 0; y < h2.height(); ++y)
        for (int x = 0; x < h2.width(); ++x)
            dropletInitialSum += h2.at(x, y);

    ErosionParams dropletParams;
    dropletParams.inertia = 0.3f;
    dropletParams.minSlope = 0.01f;
    dropletParams.capacityFactor = 4.0f;
    dropletParams.erosionFactor = 0.3f;
    dropletParams.depositFactor = 0.3f;
    dropletParams.gravity = 4.0f;
    dropletParams.evaporateRate = 0.02f;
    dropletParams.waterThreshold = 0.01f;
    dropletParams.maxLifeTime = 25;

    double carriedSediment = dropletErode(h2, dropletParams, 700, 42);
    writePGM(h2, "droplet_after.pgm");

    double dropletFinalSum = 0.0;
    for (int y = 0; y < h2.height(); ++y)
        for (int x = 0; x < h2.width(); ++x)
            dropletFinalSum += h2.at(x, y);

    double dropletTotal = dropletFinalSum + carriedSediment;
    double dropletDiff = dropletTotal - dropletInitialSum;
    double dropletRelDiff = dropletDiff / dropletInitialSum;

    std::cout << "\ndroplet erosion mass check: initial=" << dropletInitialSum
               << " final=" << dropletFinalSum
               << " carried=" << carriedSediment
               << " final+carried=" << dropletTotal
               << " diff=" << dropletDiff << " (relative=" << dropletRelDiff << ")"
               << (std::fabs(dropletRelDiff) < 1e-5 ? " OK" : " MISMATCH") << "\n";

    return 0;
}
