#include <algorithm>
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

bool sendHeightmapSnapshot(const Socket& conn, const Heightmap& h) {
    std::vector<uint8_t> hmBytes = serializeHeightmap(h);
    return writeEnvelope(conn, static_cast<uint8_t>(MessageType::Heightmap), hmBytes);
}

// PARAMS map으로 지형을 생성하고 sim=droplet/thermal에 따라 침식을 실행하면서,
// snapshotCount 단위로 나눠 그때마다 HEIGHTMAP을 스트리밍한다. thermalErode/
// dropletErode 자체는 안 바뀐다 -- 같은 함수를 여러 번, 조금씩 호출할 뿐이다
// (docs/net-protocol.md "왜 이렇게 바뀌었나" 참고). thermalErode는 완전히
// 결정론적이라 1 iteration씩 N번 호출한 것과 N iteration을 한 번에 호출한 게
// 동일하고, dropletErode는 배치마다 다른 seed로 그 시점의 실제 지형 위에
// 새 드롭릿을 흘리는 것뿐이라 각 스냅샷이 진짜 물리 상태다.
// 실패(알 수 없는 sim, 또는 도중에 연결이 끊김)하면 errorOut을 채우고 false를
// 리턴한다. 이 경우 호출자는 최선을 다해 ERROR를 보내보되, 연결이 이미 끊긴
// 상태라면 그 시도도 조용히 실패하고 다음 readEnvelope에서 정리된다.
bool runSimulation(const Socket& conn, const std::unordered_map<std::string, std::string>& params, std::string& errorOut) {
    std::string sim = getStr(params, "sim", "droplet");
    if (sim != "droplet" && sim != "thermal") {
        errorOut = "unknown sim '" + sim + "' (expected droplet or thermal)";
        return false;
    }

    int width = getInt(params, "width", 64);
    int height = getInt(params, "height", 64);
    float scale = getFloat(params, "scale", 10.0f);
    unsigned terrainSeed = static_cast<unsigned>(getInt(params, "terrainSeed", 42));
    int octaves = getInt(params, "octaves", 3);
    float persistence = getFloat(params, "persistence", 0.5f);
    float lacunarity = getFloat(params, "lacunarity", 2.0f);
    int snapshotCount = std::max(1, getInt(params, "snapshotCount", 12));

    PerlinNoise noise;
    noise.reseed(terrainSeed);
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

        int batches = std::min(snapshotCount, std::max(1, numDroplets));

        int baseBatch = numDroplets / batches;
        int remainder = numDroplets % batches;

        for (int i = 0; i < batches; ++i) {
            int batchSize = baseBatch + (i < remainder ? 1 : 0);
            if (batchSize <= 0) continue;

            unsigned batchSeed = dropletSeed + static_cast<unsigned>(i);
            dropletErode(h, p, batchSize, batchSeed);

            if (!sendHeightmapSnapshot(conn, h)) {
                errorOut = "connection lost mid-stream";
                return false;
            }
        }
    } else {
        float talusAngle = getFloat(params, "talusAngle", 0.1f);
        float erosionRate = getFloat(params, "erosionRate", 0.3f);
        int iterations = getInt(params, "iterations", 10);

        int steps = std::min(snapshotCount, std::max(1, iterations));
        int baseStep = iterations / steps;
        int remainder = iterations % steps;

        for (int i = 0; i < steps; ++i) {
            int stepIters = baseStep + (i < remainder ? 1 : 0);
            if (stepIters <= 0) continue;
            thermalErode(h, talusAngle, erosionRate, stepIters);
            if (!sendHeightmapSnapshot(conn, h)) {
                errorOut = "connection lost mid-stream";
                return false;
            }
        }
    }

    return true;
}

bool sendError(const Socket& conn, const std::string& message) {
    std::string text = "error=" + message + "\n";
    std::vector<uint8_t> payload(text.begin(), text.end());
    return writeEnvelope(conn, static_cast<uint8_t>(MessageType::Error), payload);
}

// 연결 하나에 대해 여러 번의 PARAMS -> (HEIGHTMAP...)+HEIGHTMAP_DONE 사이클을 처리한다.
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
        if (!runSimulation(conn, params, errorMsg)) {
            sendError(conn, errorMsg);  // 연결이 이미 끊겼으면 이것도 조용히 실패, 다음 루프에서 정리됨
            continue;
        }

        std::vector<uint8_t> empty;
        if (!writeEnvelope(conn, static_cast<uint8_t>(MessageType::HeightmapDone), empty)) {
            std::cout << "failed to send HEIGHTMAP_DONE\n";
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
