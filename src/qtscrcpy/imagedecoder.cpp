#include "imagedecoder.h"

#ifdef QTSCRCPY_HAVE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include <cstring>

namespace QtScrcpy {

struct ImageDecoder::Impl
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

ImageDecoder::ImageDecoder()
    : d(new Impl)
{
    av_log_set_level(AV_LOG_ERROR);
    // MJPEG decodes a whole JPEG file as a single picture, which is exactly what a screenshot is.
    d->codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (!d->codec) {
        d->error = QStringLiteral("this FFmpeg has no JPEG decoder");
        return;
    }
    d->context = avcodec_alloc_context3(d->codec);
    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->context || !d->packet || !d->frame) {
        d->error = QStringLiteral("out of memory");
        return;
    }
    d->context->thread_count = 1;
    const int ret = avcodec_open2(d->context, d->codec, nullptr);
    if (ret < 0) {
        d->error = QStringLiteral("cannot open the JPEG decoder: %1").arg(errorText(ret));
        avcodec_free_context(&d->context);
    }
}

ImageDecoder::~ImageDecoder() = default;

bool ImageDecoder::isAvailable()
{
#ifdef QTSCRCPY_HAVE_FFMPEG
    return avcodec_find_decoder(AV_CODEC_ID_MJPEG) != nullptr;
#else
    return false;
#endif
}

bool ImageDecoder::isUsable() const
{
    return d->context != nullptr;
}

QString ImageDecoder::errorString() const
{
    return d->error;
}

VideoFrame ImageDecoder::decode(const QByteArray &data)
{
    VideoFrame out;
    if (!d->context || data.isEmpty())
        return out;

    if (av_new_packet(d->packet, data.size()) < 0) {
        d->error = QStringLiteral("out of memory");
        return out;
    }
    memcpy(d->packet->data, data.constData(), size_t(data.size()));
    int ret = avcodec_send_packet(d->context, d->packet);
    av_packet_unref(d->packet);
    if (ret < 0)
        return out; // not a picture: leave it to the caller to report

    ret = avcodec_receive_frame(d->context, d->frame);
    if (ret < 0) {
        av_frame_unref(d->frame);
        return out;
    }

    const int width = d->frame->width;
    const int height = d->frame->height;
    if (!d->scaler || width != d->scalerWidth || height != d->scalerHeight || d->frame->format != d->scalerFormat) {
        sws_freeContext(d->scaler);
        d->scaler = sws_getContext(width, height, AVPixelFormat(d->frame->format), width, height, AV_PIX_FMT_BGRA,
                                   SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        d->scalerWidth = width;
        d->scalerHeight = height;
        d->scalerFormat = d->frame->format;
    }
    if (d->scaler) {
        out.width = width;
        out.height = height;
        out.bgra.resize(width * height * 4);
        uint8_t *dst[4] = {reinterpret_cast<uint8_t *>(out.bgra.data()), nullptr, nullptr, nullptr};
        const int dstStride[4] = {width * 4, 0, 0, 0};
        sws_scale(d->scaler, d->frame->data, d->frame->linesize, 0, height, dst, dstStride);
    } else {
        d->error = QStringLiteral("cannot convert pixel format %1").arg(d->frame->format);
    }
    av_frame_unref(d->frame);
    return out;
}

} // namespace QtScrcpy

#else // !QTSCRCPY_HAVE_FFMPEG

namespace QtScrcpy {

struct ImageDecoder::Impl
{
    QString error = QStringLiteral("this build of qt-scrcpy has no FFmpeg");
};

ImageDecoder::ImageDecoder() : d(new Impl) {}
ImageDecoder::~ImageDecoder() = default;
bool ImageDecoder::isAvailable() { return false; }
bool ImageDecoder::isUsable() const { return false; }
QString ImageDecoder::errorString() const { return d->error; }
VideoFrame ImageDecoder::decode(const QByteArray &) { return {}; }

} // namespace QtScrcpy

#endif
