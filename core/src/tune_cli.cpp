#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>

#include "heightmap.h"
#include "erosion/droplet_erosion.h"
#include "erosion/thermal_erosion.h"
#include "noise/perlin_noise.h"
#include "pgm.h"

// tools/tuner_server.py용 CLI: --sim=droplet|thermal 로 어떤 침식 시뮬레이션을
// 돌릴지 고르고, 나머지 파라미터는 --key=value로 받아 한 번 실행한 뒤
// before/after PGM + mass-check 한 줄을 출력한다. 실제 침식 로직은
// core/src/erosion/의 기존 함수를 그대로 호출하므로 여기서 재구현하지 않는다.
// 나중에 physics(Phase 2) 같은 새 시뮬레이션이 추가되면 이 dispatch에 분기만
// 추가하면 tools/ 쪽은 manifest 항목만 늘리면 됨 (프론트엔드 코드 변경 불필요).

namespace {

std::unordered_map<std::string, std::string> parseArgs(int argc, char** argv) {
    std::unordered_map<std::string, std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto eq = arg.find('=');
        if (arg.rfind("--", 0) == 0 && eq != std::string::npos) {
            args[arg.substr(2, eq - 2)] = arg.substr(eq + 1);
        }
    }
    return args;
}

float getFloat(const std::unordered_map<std::string, std::string>& args, const std::string& key, float def) {
    auto it = args.find(key);
    return it != args.end() ? std::stof(it->second) : def;
}

int getInt(const std::unordered_map<std::string, std::string>& args, const std::string& key, int def) {
    auto it = args.find(key);
    return it != args.end() ? std::stoi(it->second) : def;
}

std::string getStr(const std::unordered_map<std::string, std::string>& args, const std::string& key, const std::string& def) {
    auto it = args.find(key);
    return it != args.end() ? it->second : def;
}

double sumOf(const Heightmap& h) {
    double total = 0.0;
    for (int y = 0; y < h.height(); ++y)
        for (int x = 0; x < h.width(); ++x)
            total += h.at(x, y);
    return total;
}

void expandRange(const Heightmap& h, float& minVal, float& maxVal) {
    for (int y = 0; y < h.height(); ++y) {
        for (int x = 0; x < h.width(); ++x) {
            minVal = std::min(minVal, h.at(x, y));
            maxVal = std::max(maxVal, h.at(x, y));
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    auto args = parseArgs(argc, argv);
    std::string sim = getStr(args, "sim", "droplet");

    int width = getInt(args, "width", 64);
    int height = getInt(args, "height", 64);
    float scale = getFloat(args, "scale", 10.0f);
    unsigned terrainSeed = static_cast<unsigned>(getInt(args, "terrainSeed", 42));
    int octaves = getInt(args, "octaves", 3);
    float persistence = getFloat(args, "persistence", 0.5f);
    float lacunarity = getFloat(args, "lacunarity", 2.0f);

    std::string outBefore = getStr(args, "outBefore", "before.pgm");
    std::string outAfter = getStr(args, "outAfter", "after.pgm");

    PerlinNoise noise(terrainSeed);
    Heightmap h(width, height);
    for (int y = 0; y < h.height(); ++y) {
        for (int x = 0; x < h.width(); ++x) {
            h.at(x, y) = noise.fbm((float)x / scale, (float)y / scale, octaves, persistence, lacunarity);
        }
    }
    Heightmap before = h;  // 침식 전 스냅샷 -- shared min/max 계산 및 before.pgm에 씀
    double initialSum = sumOf(h);

    double carried = 0.0;
    if (sim == "droplet") {
        ErosionParams params;
        params.inertia = getFloat(args, "inertia", 0.3f);
        params.minSlope = getFloat(args, "minSlope", 0.01f);
        params.capacityFactor = getFloat(args, "capacityFactor", 4.0f);
        params.erosionFactor = getFloat(args, "erosionFactor", 0.3f);
        params.depositFactor = getFloat(args, "depositFactor", 0.3f);
        params.gravity = getFloat(args, "gravity", 4.0f);
        params.evaporateRate = getFloat(args, "evaporateRate", 0.02f);
        params.waterThreshold = getFloat(args, "waterThreshold", 0.01f);
        params.maxLifeTime = getInt(args, "maxLifeTime", 25);

        int numDroplets = getInt(args, "numDroplets", 3000);
        unsigned dropletSeed = static_cast<unsigned>(getInt(args, "dropletSeed", 42));

        carried = dropletErode(h, params, numDroplets, dropletSeed);
    } else if (sim == "thermal") {
        float talusAngle = getFloat(args, "talusAngle", 0.1f);
        float erosionRate = getFloat(args, "erosionRate", 0.3f);
        int iterations = getInt(args, "iterations", 10);

        thermalErode(h, talusAngle, erosionRate, iterations);
    } else {
        std::cerr << "unknown --sim=" << sim << " (expected droplet or thermal)\n";
        return 1;
    }

    float sharedMin = before.at(0, 0);
    float sharedMax = before.at(0, 0);
    expandRange(before, sharedMin, sharedMax);
    expandRange(h, sharedMin, sharedMax);
    writePGM(before, outBefore, sharedMin, sharedMax);
    writePGM(h, outAfter, sharedMin, sharedMax);

    double finalSum = sumOf(h);

    double total = finalSum + carried;
    double diff = total - initialSum;
    double relDiff = diff / initialSum;

    std::cout << "MASSCHECK initial=" << initialSum
               << " final=" << finalSum
               << " carried=" << carried
               << " relDiff=" << relDiff << "\n";

    return 0;
}
