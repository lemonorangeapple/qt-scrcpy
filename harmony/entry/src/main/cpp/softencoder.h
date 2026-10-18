#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace qsc {

enum class SoftFrameType { Config, KeyFrame, Frame };
enum class SoftCodec { H264, Jpeg };

// Software encoder for devices that have no hardware one: H.264 (OpenH264) or, as a last resort, JPEG per frame.
class SoftEncoder {
public:
    // Called with one Annex B access unit, or with SPS+PPS for Config.
    using Sink = std::function<void(SoftFrameType type, int64_t timestampUs, const uint8_t *data, size_t size)>;

    static bool Available(SoftCodec codec);
    static std::unique_ptr<SoftEncoder> Create(SoftCodec codec, int width, int height, int frameRate, int bitrate,
                                               std::string &error);
    SoftCodec codec() const;
    ~SoftEncoder();

    // `rgba` is width*height*4 bytes with `stride` bytes per row, R first in memory.
    bool EncodeRgba(const uint8_t *rgba, int stride, int64_t timestampUs, const Sink &sink);
    void RequestKeyFrame();

    struct Impl;

private:
    SoftEncoder() = default;
    std::unique_ptr<Impl> d;
};

} // namespace qsc
