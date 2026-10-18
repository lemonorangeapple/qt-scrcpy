#pragma once

#include "qtscrcpy/qtscrcpy_export.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <memory>

namespace QtScrcpy {

// One decoded picture: tightly packed 32-bit BGRA (the memory layout of QImage::Format_ARGB32 on
// little-endian machines), so the library does not need Qt Gui.
struct QTSCRCPY_EXPORT VideoFrame
{
    int width = 0;
    int height = 0;
    QByteArray bgra; // width * height * 4 bytes
};

// H.264 or H.265 (Annex B) decoder backed by FFmpeg. Not thread-safe: use from one thread.
class QTSCRCPY_EXPORT H264Decoder
{
public:
    enum class Codec { H264, H265 };

    explicit H264Decoder(Codec codec = Codec::H264);
    ~H264Decoder();
    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;

    // False when this build has no FFmpeg, or the decoder cannot be created. See errorString().
    bool isUsable() const;
    QString errorString() const;

    // Decodes `data` (one or more NAL units). Returns every picture that became available;
    // usually one, none while the decoder is still waiting for a key frame. Corrupt data is
    // skipped and reported through errorString(), the decoder recovers at the next key frame.
    QList<VideoFrame> decode(const QByteArray &data);

    // Forgets all buffered state (use after a gap in the stream).
    void reset();

    static bool isAvailable();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace QtScrcpy
