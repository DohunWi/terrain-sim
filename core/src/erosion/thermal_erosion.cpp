#include "thermal_erosion.h"
#include <vector>;

std::vector<LowestNeighbor> findLowestNeighbor(const Heightmap& h, int x, int y) {
    std::vector<LowestNeighbor> candidates;
    LowestNeighbor current{x, y, h.at(x, y), 0};  // 일단 "자기 자신"으로 시작 (자기보다 낮은 이웃만 갱신되게)

    int dx[4] = {-1, 1, 0, 0};
    int dy[4] = {0, 0, -1, 1};

    for (int i = 0; i < 4; ++i) {
        int nx = x + dx[i];
        int ny = y + dy[i];

        // TODO: nx, ny가 범위 안(0 <= nx < h.width(), 0 <= ny < h.height())인지 체크
        // TODO: 범위 안이고, h.at(nx, ny)가 best.value보다 작으면 best 갱신
        //       (best = LowestNeighbor{nx, ny, h.at(nx, ny)};)
        if(0<= nx && nx < h.width() && 0 <= ny && ny < h.height()) {
            if(h.at(nx,ny) < current.value ){
                candidates.push_back(LowestNeighbor{nx, ny, h.at(nx, ny), h.at(x, y) - h.at(nx, ny)});
            }
        }
    }
    return candidates;

}   

void thermalErode(Heightmap& height, float talusAngle, float erosionRate, int iterations) {
    for (int iter = 0; iter < iterations; ++iter) {
        Heightmap next = height;  // 복사본을 만들어서 여기에 결과를 씀 (더블 버퍼링)

        for (int y = 0; y < height.height(); ++y) {
            for (int x = 0; x < height.width(); ++x) {
                std::vector<LowestNeighbor> candidates = findLowestNeighbor(height, x, y);
                float d_max = 0;
                float d_total = 0;
                for (const auto& c : candidates) {  // C#의 foreach(var c in candidates), Python의 for c in candidates
                    d_total += c.diff;
                    if(c.diff>=d_max){
                        d_max = c.diff;
                    }
                }

                if(d_max > talusAngle){
                    float moveAmount;
                    moveAmount = (d_max - talusAngle) * erosionRate;
                    next.at(x,y) -= moveAmount;
                    
                    for (const auto& c : candidates) {  // C#의 foreach(var c in candidates), Python의 for c in candidates
                        next.at(c.x,c.y) += moveAmount*c.diff/d_total;
                    }
                }
            }   
        }   

        height = next;  // 이번 iteration 결과를 반영
    }
}