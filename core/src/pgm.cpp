#include "pgm.h"
#include <fstream>

void writePGM(const Heightmap& h, const std::string& filename){
    float minVal = h.at(0, 0);
    float maxVal = h.at(0, 0);

    for(int y=0; y<h.height(); y++){
        for(int x=0; x<h.width(); x++){
            if(h.at(x,y) >= maxVal) {
                maxVal = h.at(x,y);
            }
            if(h.at(x,y) <= minVal) {
                minVal = h.at(x,y);
            }
        }
    }

    writePGM(h, filename, minVal, maxVal);
}

void writePGM(const Heightmap& h, const std::string& filename, float minVal, float maxVal){
    std::ofstream out(filename);

    // write header
    out << "P2\n";
    out << h.width() << " " << h.height() << "\n";
    out << 255 << "\n";

    // write Pixel 
    for(int y=0; y<h.height(); y++){
        for(int x=0; x<h.width(); x++){
            int value;
            value = static_cast<int>((h.at(x, y) - minVal) / (maxVal - minVal) * 255);
            out << value << " ";
        }
        out << "\n";
    }

}