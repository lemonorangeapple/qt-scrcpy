#include "softencoder.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "third_party/stb/stb_image_write.h"

#include <algorithm>
#include <cstring>
#include <vector>

#ifdef QSC_HAVE_OPENH264
#include <codec_api.h>
#endif

namespace qsc {

struct SoftEncoder::Impl {
    SoftCodec codec = SoftCodec::H264;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> yuv;  // H.264 input: I420
    std::vector<uint8_t> rgb;  // JPEG input: tightly packed
    std::vector<uint8_t> jpeg; // JPEG output
#ifdef QSC_HAVE_OPENH264
    ISVCEncoder *encoder = nullptr;
    ~Impl()
    {
        if (encoder) {
            encoder->Uninitialize();
            WelsDestroySVCEncoder(encoder);
        }
    }
#endif
};

namespace {

constexpr int kJpegQuality = 60;

void AppendJpeg(void *context, void *data, int size)
{
    auto *out = static_cast<std::vector<uint8_t> *>(context);
    out->insert(out->end(), static_cast<uint8_t *>(data), static_cast<uint8_t *>(data) + size);
}

// Every JPEG picture stands alone, so each one is a key frame and no configuration packet exists.
bool EncodeJpeg(SoftEncoder::Impl &d, const uint8_t *rgba, int stride, int64_t timestampUs,
                const SoftEncoder::Sink &sink)
{
    for (int y = 0; y < d.height; ++y) {
        const uint8_t *src = rgba + size_t(y) * stride;
        uint8_t *dst = d.rgb.data() + size_t(y) * d.width * 3;
        for (int x = 0; x < d.width; ++x) {
            dst[x * 3] = src[x * 4];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    d.jpeg.clear();
    if (!stbi_write_jpg_to_func(AppendJpeg, &d.jpeg, d.width, d.height, 3, d.rgb.data(), kJpegQuality))
        return false;
    sink(SoftFrameType::KeyFrame, timestampUs, d.jpeg.data(), d.jpeg.size());
    return true;
}

#ifdef QSC_HAVE_OPENH264

inline uint8_t Clamp(int v)
{
    return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// BT.601 limited range, the default of H.264 decoders when the stream says nothing else.
void RgbaToI420(const uint8_t *rgba, int stride, int width, int height, uint8_t *yuv)
{
    uint8_t *yPlane = yuv;
    uint8_t *uPlane = yuv + size_t(width) * height;
    uint8_t *vPlane = uPlane + size_t(width / 2) * (height / 2);
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = rgba + size_t(y) * stride;
        for (int x = 0; x < width; ++x) {
            const int r = row[x * 4], g = row[x * 4 + 1], b = row[x * 4 + 2];
            yPlane[size_t(y) * width + x] = Clamp(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }
    }
    for (int y = 0; y < height / 2; ++y) {
        const uint8_t *row0 = rgba + size_t(y * 2) * stride;
        const uint8_t *row1 = row0 + stride;
        for (int x = 0; x < width / 2; ++x) {
            int r = 0, g = 0, b = 0;
            for (const uint8_t *p : {row0 + x * 8, row0 + x * 8 + 4, row1 + x * 8, row1 + x * 8 + 4}) {
                r += p[0];
                g += p[1];
                b += p[2];
            }
            r /= 4;
            g /= 4;
            b /= 4;
            uPlane[size_t(y) * (width / 2) + x] = Clamp(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            vPlane[size_t(y) * (width / 2) + x] = Clamp(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
}

bool CreateH264(SoftEncoder::Impl &d, int frameRate, int bitrate, std::string &error)
{
    if (WelsCreateSVCEncoder(&d.encoder) != 0 || !d.encoder) {
        error = "cannot create the OpenH264 encoder";
        return false;
    }
    SEncParamExt param;
    d.encoder->GetDefaultParams(&param);
    param.iUsageType = SCREEN_CONTENT_REAL_TIME;
    param.iPicWidth = d.width;
    param.iPicHeight = d.height;
    param.iTargetBitrate = bitrate;
    param.iMaxBitrate = bitrate * 2;
    param.iRCMode = RC_BITRATE_MODE;
    param.fMaxFrameRate = float(frameRate);
    param.iTemporalLayerNum = 1;
    param.iSpatialLayerNum = 1;
    param.uiIntraPeriod = unsigned(frameRate) * 2;
    param.bEnableFrameSkip = true;
    param.iComplexityMode = LOW_COMPLEXITY;
    param.iMultipleThreadIdc = 1; // the portable build runs the encoder on the calling thread
    param.sSpatialLayers[0].iVideoWidth = d.width;
    param.sSpatialLayers[0].iVideoHeight = d.height;
    param.sSpatialLayers[0].fFrameRate = float(frameRate);
    param.sSpatialLayers[0].iSpatialBitrate = bitrate;
    param.sSpatialLayers[0].iMaxSpatialBitrate = bitrate * 2;
    param.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
    param.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    if (d.encoder->InitializeExt(&param) != 0) {
        error = "OpenH264 rejected the encoder parameters";
        return false;
    }
    int format = videoFormatI420;
    d.encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &format);
    d.yuv.resize(size_t(d.width) * d.height * 3 / 2);
    return true;
}

bool EncodeH264(SoftEncoder::Impl &d, const uint8_t *rgba, int stride, int64_t timestampUs,
                const SoftEncoder::Sink &sink)
{
    RgbaToI420(rgba, stride, d.width, d.height, d.yuv.data());

    SSourcePicture picture;
    std::memset(&picture, 0, sizeof picture);
    picture.iColorFormat = videoFormatI420;
    picture.iPicWidth = d.width;
    picture.iPicHeight = d.height;
    picture.iStride[0] = d.width;
    picture.iStride[1] = picture.iStride[2] = d.width / 2;
    picture.pData[0] = d.yuv.data();
    picture.pData[1] = picture.pData[0] + size_t(d.width) * d.height;
    picture.pData[2] = picture.pData[1] + size_t(d.width / 2) * (d.height / 2);
    picture.uiTimeStamp = timestampUs / 1000;

    SFrameBSInfo info;
    std::memset(&info, 0, sizeof info);
    if (d.encoder->EncodeFrame(&picture, &info) != 0)
        return false;
    if (info.eFrameType == videoFrameTypeSkip || info.eFrameType == videoFrameTypeInvalid)
        return true;

    // SPS/PPS lead a key frame; hand them out as the configuration packet and the rest as the picture,
    // the way the hardware encoder delivers them.
    std::vector<uint8_t> config;
    std::vector<uint8_t> picturePayload;
    for (int layer = 0; layer < info.iLayerNum; ++layer) {
        const SLayerBSInfo &l = info.sLayerInfo[layer];
        const uint8_t *p = l.pBsBuf;
        for (int nal = 0; nal < l.iNalCount; ++nal) {
            const int size = l.pNalLengthInByte[nal];
            const int header = (p[2] == 1) ? 3 : 4;
            const int type = p[header] & 0x1f;
            std::vector<uint8_t> &target = (type == 7 || type == 8) ? config : picturePayload;
            target.insert(target.end(), p, p + size);
            p += size;
        }
    }
    if (!config.empty())
        sink(SoftFrameType::Config, timestampUs, config.data(), config.size());
    if (!picturePayload.empty()) {
        const bool key = info.eFrameType == videoFrameTypeIDR || info.eFrameType == videoFrameTypeI;
        sink(key ? SoftFrameType::KeyFrame : SoftFrameType::Frame, timestampUs, picturePayload.data(),
             picturePayload.size());
    }
    return true;
}

#endif // QSC_HAVE_OPENH264

} // namespace

bool SoftEncoder::Available(SoftCodec codec)
{
#ifdef QSC_HAVE_OPENH264
    return true;
#else
    return codec == SoftCodec::Jpeg;
#endif
}

std::unique_ptr<SoftEncoder> SoftEncoder::Create(SoftCodec codec, int width, int height, int frameRate, int bitrate,
                                                 std::string &error)
{
    width &= ~1;
    height &= ~1;
    if (width < 16 || height < 16) {
        error = "picture too small for the software encoder";
        return nullptr;
    }
    std::unique_ptr<SoftEncoder> self(new SoftEncoder);
    self->d = std::make_unique<Impl>();
    self->d->codec = codec;
    self->d->width = width;
    self->d->height = height;
    if (codec == SoftCodec::Jpeg) {
        self->d->rgb.resize(size_t(width) * height * 3);
        return self;
    }
#ifdef QSC_HAVE_OPENH264
    if (!CreateH264(*self->d, frameRate, bitrate, error))
        return nullptr;
    return self;
#else
    (void)frameRate;
    (void)bitrate;
    error = "built without OpenH264";
    return nullptr;
#endif
}

SoftEncoder::~SoftEncoder() = default;

SoftCodec SoftEncoder::codec() const
{
    return d->codec;
}

void SoftEncoder::RequestKeyFrame()
{
#ifdef QSC_HAVE_OPENH264
    if (d && d->encoder)
        d->encoder->ForceIntraFrame(true);
#endif
}

bool SoftEncoder::EncodeRgba(const uint8_t *rgba, int stride, int64_t timestampUs, const Sink &sink)
{
    if (d->codec == SoftCodec::Jpeg)
        return EncodeJpeg(*d, rgba, stride, timestampUs, sink);
#ifdef QSC_HAVE_OPENH264
    return EncodeH264(*d, rgba, stride, timestampUs, sink);
#else
    return false;
#endif
}

} // namespace qsc
