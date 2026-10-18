#define QTSCRCPY_C_BUILD
#include "qtscrcpy_c.h"

#include "adb.h"
#include "devicemonitor.h"
#include "hdc.h"
#include "mirror.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

using namespace QtScrcpy;

struct qsc_session {
    MirrorSession *mirror = nullptr;
    qsc_frame_callback onFrame = nullptr;
    qsc_finished_callback onFinished = nullptr;
    void *user = nullptr;
};

namespace {

// Everything Qt lives in one thread: the thread of the host's QCoreApplication when the host has one,
// otherwise a thread of our own that creates and runs one.
struct Context {
    QThread *thread = nullptr;
    QObject *anchor = nullptr;       // lives in `thread`; target of cross-thread calls
    Adb *adb = nullptr;
    DeviceMonitor *monitor = nullptr;
    Hdc *hdc = nullptr;
    bool watching = false;
    std::set<qsc_session *> sessions;

    std::thread owner;               // runs the event loop when we created the application
    QCoreApplication *app = nullptr; // set when `owner` runs one
};

std::mutex g_mutex;
Context *g_ctx = nullptr;
int g_refs = 0;

QString ownDirectory()
{
#ifdef _WIN32
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&qsc_init), &module)) {
        wchar_t path[MAX_PATH * 4];
        const DWORD n = GetModuleFileNameW(module, path, DWORD(sizeof path / sizeof path[0]));
        if (n > 0 && n < sizeof path / sizeof path[0])
            return QFileInfo(QString::fromWCharArray(path, int(n))).absolutePath();
    }
#else
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&qsc_init), &info) && info.dli_fname)
        return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).absolutePath();
#endif
    return {};
}

// Looks for adb, hdc and scrcpy-server in `directory`, unless the user already named them.
void prepareEnvironment(const QString &directory)
{
    if (directory.isEmpty())
        return;
    const struct { const char *variable; const char *file; } wanted[] = {
        {"ADB", "adb"}, {"HDC", "hdc"}, {"SCRCPY_SERVER_PATH", "scrcpy-server"},
    };
    for (const auto &item : wanted) {
        if (!qEnvironmentVariableIsEmpty(item.variable))
            continue;
        QString candidate = QDir(directory).filePath(QString::fromLatin1(item.file));
#ifdef _WIN32
        if (strcmp(item.file, "scrcpy-server") != 0)
            candidate += QStringLiteral(".exe");
#endif
        if (QFileInfo(candidate).isFile())
            qputenv(item.variable, QDir::toNativeSeparators(candidate).toLocal8Bit());
    }
}

void buildObjects(Context *ctx)
{
    ctx->thread = QThread::currentThread();
    ctx->anchor = new QObject;
    ctx->adb = new Adb;
    ctx->monitor = new DeviceMonitor(ctx->adb);
    ctx->hdc = new Hdc;
}

void destroyObjects(Context *ctx)
{
    // Sessions the caller forgot: they cannot outlive the library.
    for (qsc_session *session : std::vector<qsc_session *>(ctx->sessions.begin(), ctx->sessions.end())) {
        session->mirror->disconnect();
        delete session->mirror;
        delete session;
    }
    ctx->sessions.clear();
    delete ctx->monitor;
    delete ctx->hdc;
    delete ctx->adb;
    delete ctx->anchor;
    ctx->monitor = nullptr;
    ctx->hdc = nullptr;
    ctx->adb = nullptr;
    ctx->anchor = nullptr;
}

// Runs `f` in the Qt thread and waits for it (directly when already there, e.g. inside a callback).
template <typename F>
void inContext(Context *ctx, F f)
{
    if (QThread::currentThread() == ctx->thread)
        f();
    else
        QMetaObject::invokeMethod(ctx->anchor, f, Qt::BlockingQueuedConnection);
}

void copyString(char *to, size_t size, const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    size_t n = qMin(size_t(utf8.size()), size - 1);
    // Never end in the middle of a multi-byte character: back up to the start of the one that was cut.
    if (n < size_t(utf8.size())) {
        while (n > 0 && (static_cast<unsigned char>(utf8[int(n)]) & 0xC0) == 0x80)
            --n;
    }
    memcpy(to, utf8.constData(), n);
    to[n] = '\0';
}

int stateOf(const AdbDevice &device)
{
    if (device.state == AdbDevice::State::Device)
        return QSC_DEVICE_ONLINE;
    if (device.state == AdbDevice::State::Unauthorized)
        return QSC_DEVICE_UNAUTHORIZED;
    return QSC_DEVICE_OFFLINE;
}

} // namespace

extern "C" {

int QSC_CALL qsc_interface_version(void)
{
    return QSC_VERSION_MAJOR * 100 + QSC_VERSION_MINOR;
}

int QSC_CALL qsc_init(const char *directory)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ctx) {
        ++g_refs;
        return 0;
    }
    prepareEnvironment(directory && *directory ? QString::fromUtf8(directory) : ownDirectory());

    auto ctx = std::make_unique<Context>();
    if (QCoreApplication *host = QCoreApplication::instance()) {
        // A Qt host: use its thread. Build the objects there.
        ctx->thread = host->thread();
        Context *raw = ctx.get();
        QObject trampoline; // lives in the calling thread; only used to post the work
        trampoline.moveToThread(host->thread());
        if (QThread::currentThread() == host->thread())
            buildObjects(raw);
        else
            QMetaObject::invokeMethod(&trampoline, [raw] { buildObjects(raw); }, Qt::BlockingQueuedConnection);
        trampoline.moveToThread(QThread::currentThread());
    } else {
        Context *raw = ctx.get();
        std::mutex readyMutex;
        std::condition_variable readyCondition;
        bool ready = false;
        ctx->owner = std::thread([raw, &readyMutex, &readyCondition, &ready] {
            static int argc = 1;
            static char arg0[] = "qtscrcpy";
            static char *argv[] = {arg0, nullptr};
            QCoreApplication app(argc, argv);
            raw->app = &app;
            buildObjects(raw);
            {
                std::lock_guard<std::mutex> readyLock(readyMutex);
                ready = true;
            }
            readyCondition.notify_one();
            app.exec();
            destroyObjects(raw);
            raw->app = nullptr;
        });
        std::unique_lock<std::mutex> readyLock(readyMutex);
        readyCondition.wait(readyLock, [&ready] { return ready; });
    }
    g_ctx = ctx.release();
    g_refs = 1;
    return 0;
}

void QSC_CALL qsc_shutdown(void)
{
    Context *ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_ctx || --g_refs > 0)
            return;
        ctx = g_ctx;
        g_ctx = nullptr;
    }
    if (ctx->owner.joinable()) {
        QMetaObject::invokeMethod(ctx->anchor, [ctx] { ctx->app->quit(); }, Qt::QueuedConnection);
        ctx->owner.join();
    } else {
        inContext(ctx, [ctx] { destroyObjects(ctx); });
    }
    delete ctx;
}

int QSC_CALL qsc_can_decode(void)
{
    return H264Decoder::isAvailable() ? 1 : 0;
}

int QSC_CALL qsc_backend_available(int backend)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Context *ctx = g_ctx;
    if (!ctx)
        return 0;
    int available = 0;
    inContext(ctx, [&] { available = (backend == QSC_BACKEND_HDC ? ctx->hdc->isAvailable() : ctx->adb->isAvailable()) ? 1 : 0; });
    return available;
}

int QSC_CALL qsc_list_devices(qsc_device *out, int capacity)
{
    Context *ctx;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ctx = g_ctx;
    }
    if (!ctx)
        return 0;
    int total = 0;
    inContext(ctx, [&] {
        if (!ctx->watching) {
            ctx->watching = true;
            if (ctx->adb->isAvailable())
                ctx->monitor->start();
            if (ctx->hdc->isAvailable())
                ctx->hdc->startPolling();
        }
        const auto add = [&](const AdbDevice &device, int backend) {
            if (out && total < capacity) {
                qsc_device &d = out[total];
                memset(&d, 0, sizeof d);
                d.backend = backend;
                d.state = stateOf(device);
                copyString(d.serial, sizeof d.serial, device.serial);
                copyString(d.name, sizeof d.name, device.displayName());
            }
            ++total;
        };
        for (const AdbDevice &device : ctx->monitor->devices())
            add(device, QSC_BACKEND_ADB);
        for (const AdbDevice &device : ctx->hdc->targets())
            add(device, QSC_BACKEND_HDC);
    });
    return total;
}

const char *QSC_CALL qsc_source_unsupported(int backend, int source)
{
    // A plain character buffer: a thread_local object with a destructor (QByteArray) is destroyed on
    // thread exit, which on MinGW can happen after Qt is already gone.
    static thread_local char text[256];
    const char *fixed = nullptr;
    if (backend != QSC_BACKEND_ADB && backend != QSC_BACKEND_HDC)
        fixed = "unknown backend";
    else if (source < QSC_SOURCE_SCREEN || source > QSC_SOURCE_CAMERA_EXTERNAL)
        fixed = "unknown source";
    if (fixed) {
        snprintf(text, sizeof text, "%s", fixed);
        return text;
    }
    const QString reason = MirrorSession::unsupportedReason(backend == QSC_BACKEND_HDC ? Backend::Hdc : Backend::Adb,
                                                            static_cast<MirrorSource>(source));
    if (reason.isEmpty())
        return nullptr;
    copyString(text, sizeof text, reason);
    return text;
}

qsc_session *QSC_CALL qsc_session_create(int backend, const char *serial, int source)
{
    if (!serial || !*serial || (backend != QSC_BACKEND_ADB && backend != QSC_BACKEND_HDC)
        || source < QSC_SOURCE_SCREEN || source > QSC_SOURCE_CAMERA_EXTERNAL)
        return nullptr;
    Context *ctx;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ctx = g_ctx;
    }
    if (!ctx)
        return nullptr;

    auto *session = new qsc_session;
    MirrorOptions options;
    options.backend = backend == QSC_BACKEND_HDC ? Backend::Hdc : Backend::Adb;
    options.serial = QString::fromUtf8(serial);
    options.source = static_cast<MirrorSource>(source);
    inContext(ctx, [&] {
        session->mirror = new MirrorSession(options, ctx->hdc, ctx->adb->executable());
        qsc_session *s = session;
        QObject::connect(s->mirror, &MirrorSession::frameReady, s->mirror, [s](const VideoFrame &frame) {
            if (!s->onFrame || frame.width <= 0 || frame.height <= 0)
                return;
            qsc_frame f{frame.width, frame.height, reinterpret_cast<const uint8_t *>(frame.bgra.constData())};
            s->onFrame(&f, s->user);
        });
        QObject::connect(s->mirror, &MirrorSession::finished, s->mirror, [s](const QString &error) {
            if (!s->onFinished)
                return;
            const QByteArray utf8 = error.toUtf8();
            s->onFinished(error.isEmpty() ? nullptr : utf8.constData(), s->user);
        });
        ctx->sessions.insert(session);
    });
    return session;
}

void QSC_CALL qsc_session_set_limits(qsc_session *s, int max_size, int bit_rate, int max_fps)
{
    Context *ctx;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ctx = g_ctx;
    }
    if (!s || !ctx)
        return;
    inContext(ctx, [&] {
        if (!ctx->sessions.count(s) || s->mirror->state() != MirrorSession::State::Stopped)
            return;
        // The options are fixed when the session object is built: rebuild it with the limits.
        MirrorOptions options = s->mirror->options();
        options.maxSize = max_size > 0 ? max_size : 0;
        options.bitRate = bit_rate > 0 ? bit_rate : 0;
        options.maxFps = max_fps > 0 ? max_fps : 0;
        s->mirror->disconnect();
        delete s->mirror;
        s->mirror = new MirrorSession(options, ctx->hdc, ctx->adb->executable());
        QObject::connect(s->mirror, &MirrorSession::frameReady, s->mirror, [s](const VideoFrame &frame) {
            if (!s->onFrame || frame.width <= 0 || frame.height <= 0)
                return;
            qsc_frame f{frame.width, frame.height, reinterpret_cast<const uint8_t *>(frame.bgra.constData())};
            s->onFrame(&f, s->user);
        });
        QObject::connect(s->mirror, &MirrorSession::finished, s->mirror, [s](const QString &error) {
            if (!s->onFinished)
                return;
            const QByteArray utf8 = error.toUtf8();
            s->onFinished(error.isEmpty() ? nullptr : utf8.constData(), s->user);
        });
    });
}

int QSC_CALL qsc_session_start(qsc_session *s, qsc_frame_callback on_frame, qsc_finished_callback on_finished, void *user)
{
    Context *ctx;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ctx = g_ctx;
    }
    if (!s || !ctx)
        return -1;
    int result = -1;
    inContext(ctx, [&] {
        if (!ctx->sessions.count(s) || s->mirror->state() != MirrorSession::State::Stopped)
            return;
        s->onFrame = on_frame;
        s->onFinished = on_finished;
        s->user = user;
        s->mirror->start();
        result = 0;
    });
    return result;
}

void QSC_CALL qsc_session_stop(qsc_session *s)
{
    Context *ctx;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ctx = g_ctx;
    }
    if (!s || !ctx)
        return;
    inContext(ctx, [&] {
        if (ctx->sessions.count(s))
            s->mirror->stop();
    });
}

int QSC_CALL qsc_session_state(const qsc_session *s)
{
    Context *ctx;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ctx = g_ctx;
    }
    if (!s || !ctx)
        return QSC_SESSION_STOPPED;
    int state = QSC_SESSION_STOPPED;
    inContext(ctx, [&] {
        qsc_session *key = const_cast<qsc_session *>(s);
        if (ctx->sessions.count(key))
            state = int(key->mirror->state());
    });
    return state;
}

void QSC_CALL qsc_session_destroy(qsc_session *s)
{
    Context *ctx;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        ctx = g_ctx;
    }
    if (!s || !ctx)
        return;
    inContext(ctx, [&] {
        if (!ctx->sessions.erase(s))
            return;
        s->onFrame = nullptr;
        s->onFinished = nullptr;
        s->mirror->disconnect();
        s->mirror->stop();
        delete s->mirror; // waits for the worker thread; no callback can run after this
        delete s;
    });
}

} // extern "C"
