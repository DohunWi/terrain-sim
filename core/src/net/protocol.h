#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include "socket.h"
#include "heightmap.h"

enum class MessageType : uint8_t {
    Params = 0x01,
    Heightmap = 0x02,
    Error = 0x03,
};

struct Envelope{
    uint8_t type; 
    std::vector<uint8_t> payload; 
};

std::optional<Envelope> readEnvelope(const Socket& sock);

bool writeEnvelope(const Socket& sock, uint8_t type, const std::vector<uint8_t>& payload);

std::vector<uint8_t> serializeHeightmap(const Heightmap& hm);