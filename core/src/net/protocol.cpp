#include "protocol.h"
#include "io.h"
#include <bit>

std::optional<Envelope> readEnvelope(const Socket& sock){
    // 1단계: 헤더 5바이트 담을 자리를 만들고, recvAll로 채운다
    uint8_t header[5];
    if (!recvAll(sock, header, 5)) {
        return std::nullopt;   // 못 받았으면 "값 없음"으로 실패 표현
    }
    // 2단계: header[0..3]을 length로 조립, header[4]를 type으로
    uint32_t length = (uint32_t)header[0] + (uint32_t)header[1]*256 + (uint32_t)header[2]*256*256 + (uint32_t)header[3]*256*256*256;
    uint8_t type = header[4];

    // 3단계: length바이트짜리 자리를 만들고, recvAll로 채운다
    std::vector<uint8_t> payload(length);   // length개의 원소로 미리 크기 잡힌 벡터
    if (!recvAll(sock, payload.data(), length)) {
        return std::nullopt;
    }

    // 4단계: 다 묶어서 리턴 (Envelope{...}가 자동으로 optional에 담김)
    return Envelope{type, payload};
}
bool writeEnvelope(const Socket& sock, uint8_t type, const std::vector<uint8_t>& payload){
    uint32_t length = payload.size();

    uint8_t header[5];
    header[0] = length & 0xFF;
    header[1] = (length >> 8) & 0xFF;
    header[2] = (length >> 16) & 0xFF;
    header[3] = (length >> 24) & 0xFF;
    header[4] = type;

    if(!sendAll(sock, header, 5)){ return false; };

    if(!sendAll(sock, payload.data(), payload.size())){ return false; };

    return true;
}

std::vector<uint8_t> serializeHeightmap(const Heightmap& hm){
    std::vector<uint8_t> bytes;
    uint32_t w = hm.width();
    uint32_t h = hm.height();

    uint8_t width[4];
    width[0] = w & 0xFF;
    bytes.push_back(width[0]);
    width[1] = (w >> 8) & 0xFF;
    bytes.push_back(width[1]);
    width[2] = (w >> 16) & 0xFF;
    bytes.push_back(width[2]);
    width[3] = (w >> 24) & 0xFF;
    bytes.push_back(width[3]);

    uint8_t height[4];
    height[0] = h & 0xFF;
    bytes.push_back(height[0]);
    height[1] = (h >> 8) & 0xFF;
    bytes.push_back(height[1]);
    height[2] = (h >> 16) & 0xFF;
    bytes.push_back(height[2]);
    height[3] = (h >> 24) & 0xFF;
    bytes.push_back(height[3]);

    for(size_t i=0; i<h; i++){
        for(size_t j=0; j<w; j++){
            float f = hm.at(j,i);
            uint32_t bits = std::bit_cast<uint32_t>(f);
            uint8_t byte[4];
            byte[0] = bits & 0xFF;
            bytes.push_back(byte[0]);
            byte[1] = (bits >> 8) & 0xFF;
            bytes.push_back(byte[1]);
            byte[2] = (bits >> 16) & 0xFF;
            bytes.push_back(byte[2]);
            byte[3] = (bits >> 24) & 0xFF;
            bytes.push_back(byte[3]);
        }
    }

    return bytes;
}