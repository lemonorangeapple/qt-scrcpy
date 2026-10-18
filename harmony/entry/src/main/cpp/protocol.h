#pragma once

// Wire format shared with the computer side: see harmony/PROTOCOL.md.

#include <cstdint>
#include <vector>

namespace qsc::proto {

constexpr uint32_t kMaxPayload = 8u * 1024 * 1024;

enum class PacketType : uint8_t { Config = 0, KeyFrame = 1, Frame = 2 };

inline void PutU32(std::vector<uint8_t> &out, uint32_t v)
{
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

enum class Codec : uint8_t { H264 = 0, H265 = 1, Jpeg = 2 };

inline std::vector<uint8_t> MakeHeader(uint8_t source, Codec codec, uint32_t width, uint32_t height)
{
    std::vector<uint8_t> out = {'Q', 'S', 'C', '1', source, uint8_t(codec), 0, 0};
    PutU32(out, width);
    PutU32(out, height);
    return out;
}

inline std::vector<uint8_t> MakePacket(PacketType type, uint64_t timestampUs, const uint8_t *data, uint32_t size)
{
    std::vector<uint8_t> out;
    out.reserve(16 + size);
    out.push_back(uint8_t(type));
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
    PutU32(out, uint32_t(timestampUs >> 32));
    PutU32(out, uint32_t(timestampUs & 0xffffffffu));
    PutU32(out, size);
    out.insert(out.end(), data, data + size);
    return out;
}

} // namespace qsc::proto
