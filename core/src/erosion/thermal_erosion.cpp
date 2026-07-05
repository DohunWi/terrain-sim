#include "thermal_erosion.h"

LowestNeighbor findLowestNeighbor(const Heightmap& h, int x, int y) {
    LowestNeighbor best{x, y, h.at(x, y)};  // 일단 "자기 자신"으로 시작 (자기보다 낮은 이웃만 갱신되게)

    int dx[4] = {-1, 1, 0, 0};
    int dy[4] = {0, 0, -1, 1};

    for (int i = 0; i < 4; ++i) {
        int nx = x + dx[i];
        int ny = y + dy[i];

        // TODO: nx, ny가 범위 안(0 <= nx < h.width(), 0 <= ny < h.height())인지 체크
        // TODO: 범위 안이고, h.at(nx, ny)가 best.value보다 작으면 best 갱신
        //       (best = LowestNeighbor{nx, ny, h.at(nx, ny)};)
        if(0<= nx && nx < h.width() && 0 <= ny && ny < h.height()) {
            if(h.at(nx,ny) <= best.value ){
                best = LowestNeighbor{nx, ny, h.at(nx, ny)};
            }
        }
    }
    return best;

}   

void thermalErode(Heightmap& height, float talusAngle, float erosionRate, int iterations) {
    for (int iter = 0; iter < iterations; ++iter) {
        Heightmap next = height;  // 복사본을 만들어서 여기에 결과를 씀 (더블 버퍼링)

        for (int y = 0; y < height.height(); ++y) {
            for (int x = 0; x < height.width(); ++x) {
                LowestNeighbor n = findLowestNeighbor(height, x, y);
                float diff;
                diff = height.at(x,y) - n.value;

                if(diff > talusAngle){
                    float moveAmount;
                    moveAmount = (diff - talusAngle) * erosionRate;
                    next.at(x,y) -= moveAmount;
                    next.at(n.x, n.y) += moveAmount;
                }
                // TODO: diff = height.at(x,y) - n.value 계산 
                // TODO: diff > talusAngle이면
                //       moveAmount = (diff - talusAngle) * erosionRate
                //       next.at(x, y) -= moveAmount
                //       next.at(n.x, n.y) += moveAmount
            }   
        }   

        height = next;  // 이번 iteration 결과를 반영
    }
}