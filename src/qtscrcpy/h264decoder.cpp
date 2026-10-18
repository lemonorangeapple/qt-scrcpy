#include "h264decoder.h"

#ifdef QTSCRCPY_HAVE_FFMPEG

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

namespace QtScrcpy {

struct H264Decoder::Impl
{
    const AVCodec *codec = nullptr;
    AVCodecContext *context = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    SwsContext *scaler = nullptr;
    int scalerWidth = 0, scalerHeight = 0, scalerFormat = -1;
    QString error;

    ~Impl()
    {
        sws_freeContext(scaler);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&context);
    }
};

static QString errorText(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof buffer);
    return QString::fromUtf8(buffer);
}

H264Decoder::H264Decoder(Codec codec)
    : d(new Impl)
{
    const bool hevc = codec == Codec::H265;
    const char *name = hevc ? "H.265" : "H.264";
    // Parameter-set-only packets make the decoder log "no frame!" at warning level; real problems are errors.
    av_log_set_level(AV_LOG_ERROR);
    d->codec = avcodec_find_decoder(hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (!d->codec) {
        d->error = QStringLiteral("FFmpeg has no %1 decoder").arg(QLatin1String(name));
        return;
    }
    d->context = avcodec_alloc_context3(d->codec);
    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->context || !d->packet || !d->frame) {
        d->error = QStringLiteral("out of memory");
        return;
    }
    // Latency matters more than throughput for a live view.
    d->context->flags |= AV_CODEC_FLAG_LOW_DELAY;
    d->context->flags2 |= AV_CODEC_FLAG2_FAST;
    d->context->thread_count = 1; // frame threading would add frames of delay
    const int ret = avcodec_open2(d->context, d->codec, nullptr);
    if (ret < 0) {
        d->error = QStringLiteral("cannot open the %1 decoder: %2").arg(QLatin1String(name), errorText(ret));
        avcodec_free_context(&d->context);
    }
}

H264Decoder::~H264Decoder() = default;

bool H264Decoder::isAvailable()
{
    return true;
}

bool H264Decoder::isUsable() const
{
    return d->context != nullptr;
}

QString H264Decoder::errorString() const
{
    return d->error;
}

void H264Decoder::reset()
{
    if (d->context)
        avcodec_flush_buffers(d->context);
}

QList<VideoFrame> H264Decoder::decode(const QByteArray &data)
{
    QList<VideoFrame> frames;
    if (!d->context || data.isEmpty())
        return frames;

    // av_new_packet() allocates the zeroed padding FFmpeg's bit reader needs behind the data.
    if (av_new_packet(d->packet, data.size()) < 0) {
        d->error = QStringLiteral("out of memory");
        return frames;
    }
    memcpy(d->packet->data, data.constData(), size_t(data.size()));
    int ret = avcodec_send_packet(d->context, d->packet);
    av_packet_unref(d->packet);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        d->error = QStringLiteral("decode error: %1").arg(errorText(ret));
        return frames;
    }

    while (true) {
        ret = avcodec_receive_frame(d->context, d->frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            d->error = QStringLiteral("decode error: %1").arg(errorText(ret));
            break;
        }

        const int width = d->frame->width;
        const int height = d->frame->height;
        // sws_getCachedContext() is gone from FFmpeg 8 headers; sws_getContext() exists in all versions.
        if (!d->scaler || width != d->scalerWidth || height != d->scalerHeight || d->frame->format != d->scalerFormat) {
            sws_freeContext(d->scaler);
            d->scaler = sws_getContext(width, height, AVPixelFormat(d->frame->format), width, height, AV_PIX_FMT_BGRA,
                                       SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
            d->scalerWidth = width;
            d->scalerHeight = height;
            d->scalerFormat = d->frame->format;
        }
        if (!d->scaler) {
            d->error = QStringLiteral("cannot convert pixel format %1").arg(d->frame->format);
            av_frame_unref(d->frame);
            break;
        }
        VideoFrame out;
        out.width = width;
        out.height = height;
        out.bgra.resize(width * height * 4);
        uint8_t *dst[4] = {reinterpret_cast<uint8_t *>(out.bgra.data()), nullptr, nullptr, nullptr};
        const int dstStride[4] = {width * 4, 0, 0, 0};
        sws_scale(d->scaler, d->frame->data, d->frame->linesize, 0, height, dst, dstStride);
        av_frame_unref(d->frame);
        frames.append(out);
    }
    return frames;
}

} // namespace QtScrcpy

#else // !QTSCRCPY_HAVE_FFMPEG

namespace QtScrcpy {

struct H264Decoder::Impl
{
    QString error = QStringLiteral("this build of qt-scrcpy has no FFmpeg (configure with -DQTSCRCPY_FFMPEG_ROOT=...)");
};

H264Decoder::H264Decoder(Codec) : d(new Impl) {}
H264Decoder::~H264Decoder() = default;
bool H264Decoder::isAvailable() { return false; }
bool H264Decoder::isUsable() const { return false; }
QString H264Decoder::errorString() const { return d->error; }
void H264Decoder::reset() {}
QList<VideoFrame> H264Decoder::decode(const QByteArray &) { return {}; }

} // namespace QtScrcpy

#endif
