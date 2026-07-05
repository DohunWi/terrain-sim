#include <iostream>

#include "heightmap.h"
#include "./erosion/thermal_erosion.h"
#include "pgm.h"

int main() {
    std::cout << "terrain-sim core: build OK\n";

    Heightmap h(8, 8);         
    h.at(3, 4) = 12.5f;         
    float total = 0.0f;

    for (int y = 0; y < h.height(); ++y) {
        for (int x = 0; x < h.width(); ++x) {
            std::cout << h.at(x, y) << " ";
            total += h.at(x,y);
        }
        std::cout << "\n";
    }
    writePGM(h, "before.pgm");
    std::cout << "before erosion total :" << total << "\n";

    total = 0;
    thermalErode(h, 1.0f, 0.5f, 20);
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
