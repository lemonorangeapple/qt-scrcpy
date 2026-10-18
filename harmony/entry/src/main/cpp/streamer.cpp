#include "streamer.h"

#include "protocol.h"
#include "softencoder.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <hilog/log.h>
#include <multimedia/player_framework/native_avbuffer.h>
#include <multimedia/player_framework/native_avcapability.h>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avcodec_videoencoder.h>
#include <multimedia/player_framework/native_avformat.h>
#include <multimedia/player_framework/native_avscreen_capture.h>
#include <multimedia/player_framework/native_avscreen_capture_base.h>
#include <native_buffer/native_buffer.h>
#include <native_window/external_window.h>
#include <ohcamera/camera.h>
#include <ohcamera/camera_input.h>
#include <ohcamera/camera_manager.h>
#include <ohcamera/capture_session.h>
#include <ohcamera/video_output.h>

#undef LOG_TAG
#define LOG_TAG "qtscrcpy"
#define LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, 0x3200, LOG_TAG, __VA_ARGS__)
#define LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, 0x3200, LOG_TAG, __VA_ARGS__)

namespace qsc {

namespace {

constexpr int kKeyFrameIntervalMs = 1000;
// Frames that cannot be sent because the computer is slow are dropped, oldest first, beyond this much data.
constexpr size_t kMaxQueuedBytes = 4u * 1024 * 1024;

struct Session {
    Options options;
    int width = 0;
    int height = 0;

    // encoder
    OH_AVCodec *encoder = nullptr;
    proto::Codec codec = proto::Codec::H264;
    std::unique_ptr<SoftEncoder> soft; // set when no hardware encoder could be used
    std::mutex softMutex;
    int64_t lastSoftFrameUs = 0;
    OHNativeWindow *encoderWindow = nullptr;

    // sources
    OH_AVScreenCapture *screen = nullptr;
    Camera_Manager *cameraManager = nullptr;
    Camera_Input *cameraInput = nullptr;
    Camera_CaptureSession *cameraSession = nullptr;
    Camera_VideoOutput *videoOutput = nullptr;
    Camera_Device *cameras = nullptr;
    uint32_t cameraCount = 0;
    Camera_OutputCapability *capability = nullptr;

    // network
    int listenFd = -1;
    int clientFd = -1;

    // hand-over between the encoder callback and the sender thread
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::vector<uint8_t>> queue;
    size_t queuedBytes = 0;
    std::vector<uint8_t> config; // latest SPS/PPS packet, replayed to a new client
    bool needKeyFrame = true;    // a client may only start at a key frame
    std::atomic<bool> stopping{false};

    std::thread server;
    std::thread keyFrames;
};

std::mutex g_mutex;
Session *g_session = nullptr;
std::atomic<bool> g_running{false};
std::string g_lastError;
std::mutex g_errorMutex;

void SetError(const std::string &message)
{
    LOGE("%{public}s", message.c_str());
    std::lock_guard<std::mutex> lock(g_errorMutex);
    g_lastError = message;
}

// ---- encoder output -------------------------------------------------------------------

void OnEncoderError(OH_AVCodec *, int32_t errorCode, void *)
{
    SetError("encoder error " + std::to_string(errorCode));
}

void OnEncoderStreamChanged(OH_AVCodec *, OH_AVFormat *, void *) {}

void OnNeedInput(OH_AVCodec *, uint32_t, OH_AVBuffer *, void *) {} // surface mode: unused

void Enqueue(Session *s, std::vector<uint8_t> packet, bool isConfig, bool isKey)
{
    std::lock_guard<std::mutex> lock(s->mutex);
    if (isConfig)
        s->config = packet;
    // Without a client there is nobody to send to; keep only the parameter sets.
    if (s->clientFd < 0)
        return;
    if (s->needKeyFrame && !isKey && !isConfig)
        return; // a fresh client must begin at a key frame
    if (isKey)
        s->needKeyFrame = false;

    s->queuedBytes += packet.size();
    s->queue.push_back(std::move(packet));
    // The computer is not keeping up: drop old data and wait for the next key frame.
    while (s->queuedBytes > kMaxQueuedBytes && !s->queue.empty()) {
        s->queuedBytes -= s->queue.front().size();
        s->queue.pop_front();
        s->needKeyFrame = true;
    }
    s->wake.notify_one();
}

void OnNewOutput(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *buffer, void *userData)
{
    auto *s = static_cast<Session *>(userData);
    OH_AVCodecBufferAttr attr{};
    if (OH_AVBuffer_GetBufferAttr(buffer, &attr) == AV_ERR_OK && attr.size > 0 && !s->stopping) {
        const uint8_t *data = OH_AVBuffer_GetAddr(buffer);
        if (data != nullptr) {
            data += attr.offset;
            const bool isConfig = (attr.flags & AVCODEC_BUFFER_FLAGS_CODEC_DATA) != 0;
            const bool isKey = (attr.flags & AVCODEC_BUFFER_FLAGS_SYNC_FRAME) != 0;
            const auto type = isConfig ? proto::PacketType::Config
                                       : (isKey ? proto::PacketType::KeyFrame : proto::PacketType::Frame);
            Enqueue(s, proto::MakePacket(type, uint64_t(attr.pts), data, uint32_t(attr.size)), isConfig, isKey);
        }
    }
    OH_VideoEncoder_FreeOutputBuffer(codec, index);
}

// Hardware first, then software, then whatever the system picks for that mime type.
OH_AVCodec *CreateEncoderFor(const char *mime)
{
    for (const OH_AVCodecCategory category : {HARDWARE, SOFTWARE}) {
        OH_AVCapability *capability = OH_AVCodec_GetCapabilityByCategory(mime, true, category);
        const char *name = capability ? OH_AVCapability_GetName(capability) : nullptr;
        if (!name || !*name)
            continue;
        if (OH_AVCodec *encoder = OH_VideoEncoder_CreateByName(name)) {
            LOGI("encoder %{public}s (%{public}s)", name, category == HARDWARE ? "hardware" : "software");
            return encoder;
        }
    }
    return OH_VideoEncoder_CreateByMime(mime);
}

void RequestKeyFrame(Session *s)
{
    if (s->soft) {
        std::lock_guard<std::mutex> lock(s->softMutex);
        s->soft->RequestKeyFrame();
        return;
    }
    if (!s->encoder)
        return;
    OH_AVFormat *request = OH_AVFormat_Create();
    OH_AVFormat_SetIntValue(request, OH_MD_KEY_REQUEST_I_FRAME, 1);
    OH_VideoEncoder_SetParameter(s->encoder, request);
    OH_AVFormat_Destroy(request);
}

void ReleaseEncoder(Session *s)
{
    if (s->encoder) {
        OH_VideoEncoder_Stop(s->encoder);
        OH_VideoEncoder_Destroy(s->encoder);
        s->encoder = nullptr;
    }
    if (s->encoderWindow) {
        OH_NativeWindow_DestroyNativeWindow(s->encoderWindow);
        s->encoderWindow = nullptr;
    }
}

bool ConfigureEncoder(Session *s, proto::Codec codec, std::string &error)
{
    OH_AVCodecCallback callback = {OnEncoderError, OnEncoderStreamChanged, OnNeedInput, OnNewOutput};
    if (OH_VideoEncoder_RegisterCallback(s->encoder, callback, s) != AV_ERR_OK) {
        error = "cannot register encoder callbacks";
        return false;
    }

    OH_AVFormat *format = OH_AVFormat_Create();
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_WIDTH, s->width);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_HEIGHT, s->height);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_SURFACE_FORMAT);
    OH_AVFormat_SetDoubleValue(format, OH_MD_KEY_FRAME_RATE, double(s->options.frameRate));
    OH_AVFormat_SetLongValue(format, OH_MD_KEY_BITRATE, s->options.bitrate);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODE_BITRATE_MODE, BITRATE_MODE_VBR);
    // Baseline/Main without B-frames: a frame can be shown as soon as it is decoded (low latency).
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_PROFILE,
                            codec == proto::Codec::H265 ? HEVC_PROFILE_MAIN : AVC_PROFILE_BASELINE);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_I_FRAME_INTERVAL, kKeyFrameIntervalMs);
    const OH_AVErrCode configured = OH_VideoEncoder_Configure(s->encoder, format);
    OH_AVFormat_Destroy(format);
    if (configured != AV_ERR_OK) {
        error = "encoder rejected the configuration (" + std::to_string(int(configured)) + ")";
        return false;
    }
    if (OH_VideoEncoder_GetSurface(s->encoder, &s->encoderWindow) != AV_ERR_OK || !s->encoderWindow) {
        error = "cannot get the encoder surface";
        return false;
    }
    if (OH_VideoEncoder_Prepare(s->encoder) != AV_ERR_OK || OH_VideoEncoder_Start(s->encoder) != AV_ERR_OK) {
        error = "cannot start the encoder";
        return false;
    }
    return true;
}

// Tries H.264, then H.265, each hardware before software, and keeps the first that starts.
bool CreateEncoder(Session *s, std::string &error)
{
    struct Candidate { proto::Codec codec; const char *mime; const char *label; };
    const Candidate candidates[] = {
        {proto::Codec::H264, OH_AVCODEC_MIMETYPE_VIDEO_AVC, "H.264"},
        {proto::Codec::H265, OH_AVCODEC_MIMETYPE_VIDEO_HEVC, "H.265"},
    };
    std::string tried;
    for (const Candidate &candidate : candidates) {
        s->encoder = CreateEncoderFor(candidate.mime);
        if (!s->encoder) {
            tried += std::string(tried.empty() ? "" : ", ") + candidate.label + ": none";
            continue;
        }
        std::string reason;
        if (ConfigureEncoder(s, candidate.codec, reason)) {
            s->codec = candidate.codec;
            return true;
        }
        tried += std::string(tried.empty() ? "" : ", ") + candidate.label + ": " + reason;
        ReleaseEncoder(s);
    }
    // Software encoding takes raw frames, so it only serves the screen (the camera needs a surface).
    if (s->options.source == Source::Screen) {
        // The portable encoder is slow: keep the picture small and the frame rate modest.
        const int frameRate = std::min(s->options.frameRate, 15);
        const int shortSide = 540;
        int width = s->width, height = s->height;
        if (std::min(width, height) > shortSide) {
            const double scale = double(shortSide) / std::min(width, height);
            width = int(width * scale);
            height = int(height * scale);
        }
        width &= ~1;
        height &= ~1;
        const struct { SoftCodec soft; proto::Codec wire; const char *label; } software[] = {
            {SoftCodec::H264, proto::Codec::H264, "software H.264"},
            {SoftCodec::Jpeg, proto::Codec::Jpeg, "JPEG"},
        };
        for (const auto &choice : software) {
            if (!SoftEncoder::Available(choice.soft))
                continue;
            std::string softError;
            s->soft = SoftEncoder::Create(choice.soft, width, height, frameRate,
                                          std::min(s->options.bitrate, 3'000'000), softError);
            if (s->soft) {
                s->codec = choice.wire;
                s->width = width;
                s->height = height;
                s->options.frameRate = frameRate;
                LOGI("software encoder (%{public}s) %{public}dx%{public}d", choice.label, width, height);
                return true;
            }
            tried += std::string(tried.empty() ? "" : ", ") + choice.label + ": " + softError;
        }
    }
    error = "no usable video encoder on this device (" + tried + ")";
    return false;
}

// ---- sources --------------------------------------------------------------------------

void OnScreenError(OH_AVScreenCapture *, int32_t errorCode, void *)
{
    SetError("screen capture error " + std::to_string(errorCode));
}

void OnScreenState(OH_AVScreenCapture *, OH_AVScreenCaptureStateCode stateCode, void *)
{
    LOGI("screen capture state %{public}d", int(stateCode));
}

void OnScreenBuffer(OH_AVScreenCapture *, OH_AVBuffer *buffer, OH_AVScreenCaptureBufferType type, int64_t timestamp,
                    void *userData)
{
    auto *s = static_cast<Session *>(userData);
    if (type != OH_SCREEN_CAPTURE_BUFFERTYPE_VIDEO || s->stopping || !s->soft || !buffer)
        return;
    // One frame at a time: a slow encoder drops the pictures that arrive meanwhile instead of queueing them.
    std::unique_lock<std::mutex> lock(s->softMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    const int64_t nowUs = timestamp / 1000;
    if (nowUs - s->lastSoftFrameUs < 1'000'000 / std::max(1, s->options.frameRate))
        return;
    s->lastSoftFrameUs = nowUs;

    OH_NativeBuffer *native = OH_AVBuffer_GetNativeBuffer(buffer);
    if (!native)
        return;
    OH_NativeBuffer_Config config{};
    void *address = nullptr;
    if (OH_NativeBuffer_MapAndGetConfig(native, &address, &config) != 0 || !address) {
        LOGE("cannot map the screen buffer");
        return;
    }
    // The capture is made at the encoder's size by StartScreen() asking for it; a mismatch is skipped.
    if (config.width >= s->width && config.height >= s->height) {
        s->soft->EncodeRgba(static_cast<const uint8_t *>(address), config.stride, nowUs,
                            [s](SoftFrameType kind, int64_t pts, const uint8_t *data, size_t size) {
                                const bool isConfig = kind == SoftFrameType::Config;
                                const bool isKey = kind == SoftFrameType::KeyFrame;
                                const auto packetType = isConfig ? proto::PacketType::Config
                                                        : isKey  ? proto::PacketType::KeyFrame
                                                                 : proto::PacketType::Frame;
                                Enqueue(s, proto::MakePacket(packetType, uint64_t(pts), data, uint32_t(size)),
                                        isConfig, isKey);
                            });
    }
    OH_NativeBuffer_Unmap(native);
}

bool StartScreenRaw(Session *s, std::string &error)
{
    s->screen = OH_AVScreenCapture_Create();
    if (!s->screen) {
        error = "cannot create the screen capture";
        return false;
    }
    OH_AVScreenCaptureConfig config{};
    config.captureMode = OH_CAPTURE_HOME_SCREEN;
    config.dataType = OH_ORIGINAL_STREAM;
    config.videoInfo.videoCapInfo.videoFrameWidth = s->width;
    config.videoInfo.videoCapInfo.videoFrameHeight = s->height;
    config.videoInfo.videoCapInfo.videoSource = OH_VIDEO_SOURCE_SURFACE_RGBA;
    OH_AVScreenCapture_SetErrorCallback(s->screen, OnScreenError, s);
    OH_AVScreenCapture_SetStateCallback(s->screen, OnScreenState, s);
    OH_AVScreenCapture_SetDataCallback(s->screen, OnScreenBuffer, s);
    if (OH_AVScreenCapture_Init(s->screen, config) != AV_SCREEN_CAPTURE_ERR_OK) {
        error = "screen capture refused the configuration";
        return false;
    }
    if (OH_AVScreenCapture_StartScreenCapture(s->screen) != AV_SCREEN_CAPTURE_ERR_OK) {
        error = "screen capture could not start (was the permission refused?)";
        return false;
    }
    return true;
}

bool StartScreen(Session *s, std::string &error)
{
    if (s->soft)
        return StartScreenRaw(s, error);
    s->screen = OH_AVScreenCapture_Create();
    if (!s->screen) {
        error = "cannot create the screen capture";
        return false;
    }
    OH_AVScreenCaptureConfig config{};
    config.captureMode = OH_CAPTURE_HOME_SCREEN;
    config.dataType = OH_ORIGINAL_STREAM;
    config.videoInfo.videoCapInfo.videoFrameWidth = s->width;
    config.videoInfo.videoCapInfo.videoFrameHeight = s->height;
    config.videoInfo.videoCapInfo.videoSource = OH_VIDEO_SOURCE_SURFACE_RGBA;
    OH_AVScreenCapture_SetErrorCallback(s->screen, OnScreenError, s);
    OH_AVScreenCapture_SetStateCallback(s->screen, OnScreenState, s);
    if (OH_AVScreenCapture_Init(s->screen, config) != AV_SCREEN_CAPTURE_ERR_OK) {
        error = "screen capture refused the configuration";
        return false;
    }
    // Shows the system "start recording?" prompt: the user has to accept it on the phone.
    if (OH_AVScreenCapture_StartScreenCaptureWithSurface(s->screen, s->encoderWindow) != AV_SCREEN_CAPTURE_ERR_OK) {
        error = "screen capture could not start (was the permission refused?)";
        return false;
    }
    return true;
}

bool StartCamera(Session *s, bool front, std::string &error)
{
    if (OH_Camera_GetCameraManager(&s->cameraManager) != CAMERA_OK || !s->cameraManager) {
        error = "no camera service";
        return false;
    }
    if (OH_CameraManager_GetSupportedCameras(s->cameraManager, &s->cameras, &s->cameraCount) != CAMERA_OK
        || s->cameraCount == 0) {
        error = "this device has no camera";
        return false;
    }
    const Camera_Position wanted = front ? CAMERA_POSITION_FRONT : CAMERA_POSITION_BACK;
    Camera_Device *device = nullptr;
    for (uint32_t i = 0; i < s->cameraCount; ++i) {
        if (s->cameras[i].cameraPosition == wanted) {
            device = &s->cameras[i];
            break;
        }
    }
    if (!device) {
        error = front ? "no front camera" : "no back camera";
        return false;
    }
    if (OH_CameraManager_GetSupportedCameraOutputCapability(s->cameraManager, device, &s->capability) != CAMERA_OK
        || !s->capability || s->capability->videoProfilesSize == 0) {
        error = "camera reports no video profiles";
        return false;
    }

    // Pick the profile whose size is closest to (but not above) the requested one, in a format the
    // encoder surface takes.
    const Camera_VideoProfile *best = nullptr;
    for (uint32_t i = 0; i < s->capability->videoProfilesSize; ++i) {
        const Camera_VideoProfile *p = s->capability->videoProfiles[i];
        if (p->format != CAMERA_FORMAT_YUV_420_SP)
            continue;
        const bool fits = int(p->size.width) <= s->width && int(p->size.height) <= s->height;
        const bool bigger = !best || (p->size.width * p->size.height > best->size.width * best->size.height);
        if (fits && bigger)
            best = p;
    }
    if (!best) {
        error = "no usable camera video profile";
        return false;
    }
    s->width = int(best->size.width);
    s->height = int(best->size.height);
    return true; // the encoder is created with the final size by the caller, then WireCamera() runs
}

bool WireCamera(Session *s, bool front, std::string &error)
{
    Camera_Device *device = nullptr;
    for (uint32_t i = 0; i < s->cameraCount; ++i) {
        if (s->cameras[i].cameraPosition == (front ? CAMERA_POSITION_FRONT : CAMERA_POSITION_BACK)) {
            device = &s->cameras[i];
            break;
        }
    }
    const Camera_VideoProfile *profile = nullptr;
    for (uint32_t i = 0; i < s->capability->videoProfilesSize; ++i) {
        const Camera_VideoProfile *p = s->capability->videoProfiles[i];
        if (p->format == CAMERA_FORMAT_YUV_420_SP && int(p->size.width) == s->width && int(p->size.height) == s->height) {
            profile = p;
            break;
        }
    }
    if (!device || !profile) {
        error = "camera profile disappeared";
        return false;
    }

    uint64_t surfaceId = 0;
    if (OH_NativeWindow_GetSurfaceId(s->encoderWindow, &surfaceId) != 0) {
        error = "cannot get the surface id of the encoder";
        return false;
    }
    const std::string id = std::to_string(surfaceId);

    if (OH_CameraManager_CreateCameraInput(s->cameraManager, device, &s->cameraInput) != CAMERA_OK
        || OH_CameraInput_Open(s->cameraInput) != CAMERA_OK) {
        error = "cannot open the camera (is the camera permission granted?)";
        return false;
    }
    if (OH_CameraManager_CreateVideoOutput(s->cameraManager, profile, id.c_str(), &s->videoOutput) != CAMERA_OK) {
        error = "cannot create the camera video output";
        return false;
    }
    if (OH_CameraManager_CreateCaptureSession(s->cameraManager, &s->cameraSession) != CAMERA_OK
        || OH_CaptureSession_SetSessionMode(s->cameraSession, NORMAL_VIDEO) != CAMERA_OK
        || OH_CaptureSession_BeginConfig(s->cameraSession) != CAMERA_OK
        || OH_CaptureSession_AddInput(s->cameraSession, s->cameraInput) != CAMERA_OK
        || OH_CaptureSession_AddVideoOutput(s->cameraSession, s->videoOutput) != CAMERA_OK
        || OH_CaptureSession_CommitConfig(s->cameraSession) != CAMERA_OK
        || OH_CaptureSession_Start(s->cameraSession) != CAMERA_OK
        || OH_VideoOutput_Start(s->videoOutput) != CAMERA_OK) {
        error = "cannot start the camera session";
        return false;
    }
    return true;
}

// ---- network --------------------------------------------------------------------------

bool SendAll(int fd, const uint8_t *data, size_t size)
{
    while (size > 0) {
        const ssize_t n = send(fd, data, size, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += n;
        size -= size_t(n);
    }
    return true;
}

void DropClient(Session *s)
{
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->clientFd >= 0) {
        close(s->clientFd);
        s->clientFd = -1;
    }
    s->queue.clear();
    s->queuedBytes = 0;
    s->needKeyFrame = true;
}

void ServeClient(Session *s, int fd)
{
    const auto header = proto::MakeHeader(uint8_t(s->options.source), s->codec, uint32_t(s->width), uint32_t(s->height));
    if (!SendAll(fd, header.data(), header.size()))
        return;

    {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->clientFd = fd;
        s->needKeyFrame = true;
        s->queue.clear();
        s->queuedBytes = 0;
        if (!s->config.empty()) {
            s->queuedBytes += s->config.size();
            s->queue.push_back(s->config);
        }
    }
    RequestKeyFrame(s); // do not wait up to a second for the first picture

    while (!s->stopping) {
        std::vector<uint8_t> packet;
        {
            std::unique_lock<std::mutex> lock(s->mutex);
            s->wake.wait_for(lock, std::chrono::milliseconds(200),
                             [s] { return s->stopping || !s->queue.empty() || s->clientFd < 0; });
            if (s->clientFd < 0)
                return;
            if (s->queue.empty()) {
                // Detect a client that went away while the screen is static.
                char probe;
                const ssize_t n = recv(fd, &probe, 1, MSG_DONTWAIT | MSG_PEEK);
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                    return;
                continue;
            }
            packet = std::move(s->queue.front());
            s->queue.pop_front();
            s->queuedBytes -= packet.size();
        }
        if (!SendAll(fd, packet.data(), packet.size()))
            return;
    }
}

void ServerLoop(Session *s)
{
    while (!s->stopping) {
        pollfd pfd{s->listenFd, POLLIN, 0};
        const int ready = poll(&pfd, 1, 200);
        if (ready <= 0)
            continue;
        const int fd = accept(s->listenFd, nullptr, nullptr);
        if (fd < 0)
            continue;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        LOGI("client connected");
        ServeClient(s, fd);
        DropClient(s);
        LOGI("client gone");
    }
}

bool Listen(Session *s, std::string &error)
{
    s->listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listenFd < 0) {
        error = "cannot create a socket";
        return false;
    }
    int one = 1;
    setsockopt(s->listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(uint16_t(s->options.port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // only reachable through `hdc fport`
    if (bind(s->listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0 || listen(s->listenFd, 1) != 0) {
        error = "cannot listen on port " + std::to_string(s->options.port);
        return false;
    }
    return true;
}

// ---- teardown -------------------------------------------------------------------------

void Teardown(Session *s)
{
    s->stopping = true;
    s->wake.notify_all();
    if (s->listenFd >= 0) {
        shutdown(s->listenFd, SHUT_RDWR);
    }
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (s->clientFd >= 0)
            shutdown(s->clientFd, SHUT_RDWR);
    }
    if (s->server.joinable())
        s->server.join();
    if (s->keyFrames.joinable())
        s->keyFrames.join();
    if (s->listenFd >= 0)
        close(s->listenFd);

    if (s->cameraSession) {
        OH_CaptureSession_Stop(s->cameraSession);
        OH_CaptureSession_Release(s->cameraSession);
    }
    if (s->videoOutput) {
        OH_VideoOutput_Stop(s->videoOutput);
        OH_VideoOutput_Release(s->videoOutput);
    }
    if (s->cameraInput) {
        OH_CameraInput_Close(s->cameraInput);
        OH_CameraInput_Release(s->cameraInput);
    }
    if (s->capability && s->cameraManager)
        OH_CameraManager_DeleteSupportedCameraOutputCapability(s->cameraManager, s->capability);
    if (s->cameras && s->cameraManager)
        OH_CameraManager_DeleteSupportedCameras(s->cameraManager, s->cameras, s->cameraCount);
    if (s->screen) {
        OH_AVScreenCapture_StopScreenCapture(s->screen);
        OH_AVScreenCapture_Release(s->screen);
    }
    ReleaseEncoder(s);
    s->soft.reset();
}

} // namespace

bool Start(const Options &options, std::string &error)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_session) {
        error = "already streaming";
        return false;
    }
    {
        std::lock_guard<std::mutex> errorLock(g_errorMutex);
        g_lastError.clear();
    }

    auto *s = new Session;
    s->options = options;
    const bool camera = options.source != Source::Screen;
    s->width = options.width > 0 ? options.width : (camera ? 1280 : 720);
    s->height = options.height > 0 ? options.height : (camera ? 720 : 1280);

    bool ok = true;
    if (camera)
        ok = StartCamera(s, options.source == Source::FrontCamera, error); // may adjust width/height
    ok = ok && CreateEncoder(s, error);
    ok = ok && (camera ? WireCamera(s, options.source == Source::FrontCamera, error) : StartScreen(s, error));
    ok = ok && Listen(s, error);
    if (!ok) {
        Teardown(s);
        delete s;
        return false;
    }

    s->server = std::thread(ServerLoop, s);
    // Parameter sets are emitted once at start; a periodic request keeps late joiners and lossy
    // links within one second of a picture.
    s->keyFrames = std::thread([s] {
        while (!s->stopping) {
            for (int i = 0; i < kKeyFrameIntervalMs / 50 && !s->stopping; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (s->stopping)
                break;
            RequestKeyFrame(s);
        }
    });
    g_session = s;
    g_running = true;
    LOGI("streaming %{public}dx%{public}d on port %{public}d", s->width, s->height, options.port);
    return true;
}

void Stop()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_session)
        return;
    Teardown(g_session);
    delete g_session;
    g_session = nullptr;
    g_running = false;
}

bool IsRunning()
{
    return g_running;
}

std::string LastError()
{
    std::lock_guard<std::mutex> lock(g_errorMutex);
    return g_lastError;
}

} // namespace qsc
