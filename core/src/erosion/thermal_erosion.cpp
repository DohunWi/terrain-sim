#include "thermal_erosion.h"
#include <vector>

NeighborList findLowestNeighbor(const Heightmap& h, int x, int y) {
    NeighborList candidates;
    LowestNeighbor current{x, y, h.at(x, y), 0};  // 일단 "자기 자신"으로 시작 (자기보다 낮은 이웃만 갱신되게)

    int dx[4] = {-1, 1, 0, 0};
    int dy[4] = {0, 0, -1, 1};

    for (int i = 0; i < 4; ++i) {
        int nx = x + dx[i];
        int ny = y + dy[i];

        // 격자 안이면서 자기 자신보다 낮은 이웃만 후보로 모은다.
        if(0<= nx && nx < h.width() && 0 <= ny && ny < h.height()) {
            if(h.at(nx,ny) < current.value ){
                candidates.items[candidates.count++] = LowestNeighbor{nx, ny, h.at(nx, ny), h.at(x, y) - h.at(nx, ny)};
            }
        }
    }
    return candidates;

}   

void thermalErode(Heightmap& height, float talusAngle, float erosionRate, int iterations) {
    Heightmap next = height;
    for (int iter = 0; iter < iterations; ++iter) {
        next = height;  
        for (int y = 0; y < height.height(); ++y) {
            for (int x = 0; x < height.width(); ++x) {
                NeighborList candidates = findLowestNeighbor(height, x, y);
                float d_max = 0;
                float d_total = 0;
                for (int i = 0; i < candidates.count; ++i){ 
                    d_total += candidates.items[i].diff;
                    if(candidates.items[i].diff>=d_max){
                        d_max = candidates.items[i].diff;
                    }
                }

                if(d_max > talusAngle){
                    float moveAmount;
                    moveAmount = (d_max - talusAngle) * erosionRate;
                    next.at(x,y) -= moveAmount;
                    
                    for(int i = 0; i < candidates.count; ++i) {
                        next.at(candidates.items[i].x, candidates.items[i].y) += moveAmount*candidates.items[i].diff/d_total;
                    }
                }
            }   
        }   

        height = next;  // 이번 iteration 결과를 반영
    }
}