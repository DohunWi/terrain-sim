#include "thermal_erosion_pull.h"
#include "thermal_erosion.h"
#include <vector>

namespace {
struct OutflowInfo {
    float dMax = 0.0f;
    float dTotal = 0.0f;
};
}  // namespace

void thermalErodePullModel(Heightmap& height, float talusAngle, float erosionRate, int iterations) {
    Heightmap next = height;
    std::vector<OutflowInfo> outflow(height.width() * height.height());
    int dx[4] = {-1, 1, 0, 0};
    int dy[4] = {0, 0, -1, 1};

    for (int iter = 0; iter < iterations; ++iter) {
        // 1패스: 셀마다 d_max/d_total만 계산해서 outflow에 저장 (next는 아직 안 건드림)
        for (int y = 0; y < height.height(); ++y) {
            for (int x = 0; x < height.width(); ++x) {
                NeighborList candidates = findLowestNeighbor(height, x, y);
                float d_max = 0;
                float d_total = 0;
                for (int i = 0; i < candidates.count; ++i) {
                    d_total += candidates.items[i].diff;
                    if (candidates.items[i].diff >= d_max) {
                        d_max = candidates.items[i].diff;
                    }
                }
                outflow[y * height.width() + x] = {d_max, d_total};
            }
        }

        // 2패스: 자기 손실 + 4방향 이웃으로부터의 이득을 계산해서 자기 칸에만 씀
        // (남의 좌표로 next.at(...)에 쓰는 줄이 하나도 없음 -- 이게 scatter와의 차이)
        for (int y = 0; y < height.height(); ++y) {
            for (int x = 0; x < height.width(); ++x) {
                OutflowInfo my = outflow[y * height.width() + x];
                float result = height.at(x, y);

                // 1. 자기 손실
                if (my.dMax > talusAngle) {
                    result -= (my.dMax - talusAngle) * erosionRate;
                }

                // 2. 4방향 이웃으로부터 받는 것
                for (int i = 0; i < 4; ++i) {
                    int nx = x + dx[i];
                    int ny = y + dy[i];
                    if (nx < 0 || nx >= height.width() || ny < 0 || ny >= height.height()) continue;

                    float diff = height.at(nx, ny) - height.at(x, y);  // 이웃이 나보다 얼마나 높은가
                    if (diff <= 0) continue;  // 이웃이 나보다 낮거나 같으면 못 받음

                    OutflowInfo nb = outflow[ny * height.width() + nx];
                    if (nb.dMax > talusAngle) {
                        result += (nb.dMax - talusAngle) * erosionRate * diff / nb.dTotal;
                    }
                }

                next.at(x, y) = result;
            }
        }

        height = next;  // 이번 iteration 결과를 반영
    }
}
