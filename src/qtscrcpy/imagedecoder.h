#pragma once

#include "h264decoder.h"
#include "qtscrcpy/qtscrcpy_export.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace QtScrcpy {

// Decodes a complete still image (JPEG) into a VideoFrame. Used by the screenshot fallback for devices
// whose screen can only be read as one picture at a time (HarmonyOS without the streamer app).
// Not thread-safe: use from one thread.
class QTSCRCPY_EXPORT ImageDecoder
{
public:
    ImageDecoder();
    ~ImageDecoder();
    ImageDecoder(const ImageDecoder &) = delete;
    ImageDecoder &operator=(const ImageDecoder &) = delete;

    // False when this build has no FFmpeg or the decoder cannot be created; see errorString().
    bool isUsable() const;
    QString errorString() const;

    // Empty when the data is not a picture this decoder understands.
    VideoFrame decode(const QByteArray &data);

    static bool isAvailable();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace QtScrcpy
