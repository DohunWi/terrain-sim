#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "heightmap.h"
#include "noise/perlin_noise.h"
#include "erosion/droplet_erosion.h"
#include "erosion/thermal_erosion.h"
#include "net/tcp_server.h"
#include "net/protocol.h"
#include "net/params.h"

// docs/net-protocol.md v1 request-response 서버.
// 포트 9000: 이 프로토콜 전용으로 고른 고정값 (tools/tuner_server.py의 8765와 겹치지 않게).

namespace {

const int kPort = 9000;

// PARAMS map으로 지형을 생성하고 sim=droplet/thermal에 따라 침식을 실행한다.
// tune_cli.cpp와 같은 dispatch 패턴(파라미터 이름도 동일) — 실제 시뮬레이션 로직은
// core/src/erosion/의 기존 함수를 그대로 호출하므로 여기서 재구현하지 않는다.
// 실패(알 수 없는 sim)하면 errorOut을 채우고, 호출자는 리턴된 Heightmap을 무시해야 한다.
Heightmap runSimulation(const std::unordered_map<std::string, std::string>& params, std::string& errorOut) {
    std::string sim = getStr(params, "sim", "droplet");
    int width = getInt(params, "width", 64);
    int height = getInt(params, "height", 64);
    float scale = getFloat(params, "scale", 10.0f);
    unsigned terrainSeed = static_cast<unsigned>(getInt(params, "terrainSeed", 42));
    int octaves = getInt(params, "octaves", 3);
    float persistence = getFloat(params, "persistence", 0.5f);
    float lacunarity = getFloat(params, "lacunarity", 2.0f);

    PerlinNoise noise(terrainSeed);
    Heightmap h(width, height);
    for (int y = 0; y < h.height(); ++y) {
        for (int x = 0; x < h.width(); ++x) {
            h.at(x, y) = noise.fbm((float)x / scale, (float)y / scale, octaves, persistence, lacunarity);
        }
    }

    if (sim == "droplet") {
        ErosionParams p;
        p.inertia = getFloat(params, "inertia", 0.3f);
        p.minSlope = getFloat(params, "minSlope", 0.01f);
        p.capacityFactor = getFloat(params, "capacityFactor", 4.0f);
        p.erosionFactor = getFloat(params, "erosionFactor", 0.3f);
        p.depositFactor = getFloat(params, "depositFactor", 0.3f);
        p.gravity = getFloat(params, "gravity", 4.0f);
        p.evaporateRate = getFloat(params, "evaporateRate", 0.02f);
        p.waterThreshold = getFloat(params, "waterThreshold", 0.01f);
        p.maxLifeTime = getInt(params, "maxLifeTime", 25);

        int numDroplets = getInt(params, "numDroplets", 700);
        unsigned dropletSeed = static_cast<unsigned>(getInt(params, "dropletSeed", 42));
        dropletErode(h, p, numDroplets, dropletSeed);
    } else if (sim == "thermal") {
        float talusAngle = getFloat(params, "talusAngle", 0.1f);
        float erosionRate = getFloat(params, "erosionRate", 0.3f);
        int iterations = getInt(params, "iterations", 10);
        thermalErode(h, talusAngle, erosionRate, iterations);
    } else {
        errorOut = "unknown sim '" + sim + "' (expected droplet or thermal)";
    }

    return h;
}

bool sendError(const Socket& conn, const std::string& message) {
    std::string text = "error=" + message + "\n";
    std::vector<uint8_t> payload(text.begin(), text.end());
    return writeEnvelope(conn, static_cast<uint8_t>(MessageType::Error), payload);
}

// 연결 하나에 대해 여러 번의 PARAMS -> HEIGHTMAP 사이클을 처리한다.
// readEnvelope가 실패(nullopt)를 리턴하면 상대가 연결을 끊은 것으로 보고 리턴한다.
void handleConnection(const Socket& conn) {
    while (true) {
        std::optional<Envelope> envelope = readEnvelope(conn);
        if (!envelope) {
            std::cout << "client disconnected\n";
            return;
        }

        if (envelope->type != static_cast<uint8_t>(MessageType::Params)) {
            sendError(conn, "expected PARAMS message");
            continue;
        }

        std::string text(envelope->payload.begin(), envelope->payload.end());
        auto params = parseParams(text);

        std::string errorMsg;
        Heightmap result = runSimulation(params, errorMsg);
        if (!errorMsg.empty()) {
            sendError(conn, errorMsg);
            continue;
        }

        std::vector<uint8_t> hmBytes = serializeHeightmap(result);
        if (!writeEnvelope(conn, static_cast<uint8_t>(MessageType::Heightmap), hmBytes)) {
            std::cout << "failed to send HEIGHTMAP response\n";
            return;
        }
    }
}

}  // namespace

int main() {
    Socket listener = makeListenSocket(kPort);
    std::cout << "terrain-sim core: listening on port " << kPort << "\n";

    while (true) {
        Socket conn = acceptConnection(listener);
        std::cout << "client connected\n";
        handleConnection(conn);
    }
}
