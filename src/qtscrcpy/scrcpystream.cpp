#include "scrcpystream.h"

#include "adb.h"
#include "adbclient.h"
#include "locate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <cstring>

namespace QtScrcpy {

namespace {

constexpr const char *kDeviceJar = "/data/local/tmp/scrcpy-server.jar";
constexpr int kDeviceNameFieldLength = 64;
constexpr int kCodecH264 = 0x68323634; // "h264"
constexpr quint64 kFlagConfig = quint64(1) << 63;
constexpr quint64 kFlagKeyFrame = quint64(1) << 62;
constexpr quint32 kMaxPacket = 16u * 1024 * 1024;
constexpr int kMaxPendingFrames = 3;
constexpr int kConnectAttempts = 150;

quint32 be32(const uchar *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

int findFreePort()
{
    QTcpServer server;
    return server.listen(QHostAddress::LocalHost, 0) ? server.serverPort() : 0;
}

// Keeps the end of what the device-side server printed; it is the only explanation of a failed start.
class ServerLog
{
public:
    void pump(QTcpSocket *socket)
    {
        if (!socket)
            return;
        socket->waitForReadyRead(0);
        m_text += socket->readAll();
        if (m_text.size() > 4096)
            m_text.remove(0, m_text.size() - 4096);
    }
    QString lastProblem() const
    {
        const QStringList lines = QString::fromUtf8(m_text).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (int i = lines.size() - 1; i >= 0; --i) {
            const QString line = lines.at(i).trimmed();
            if (line.contains(QLatin1String("ERROR")) || line.contains(QLatin1String("Exception")))
                return line;
        }
        return lines.isEmpty() ? QString() : lines.last().trimmed();
    }

private:
    QByteArray m_text;
};

} // namespace

ScrcpyStream::ScrcpyStream(const Options &options, const QString &adbExecutable, const QString &serverJar, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_adb(adbExecutable.isEmpty() ? Adb::findExecutable() : adbExecutable)
    , m_jar(serverJar.isEmpty() ? findServerJar() : serverJar)
{
    qRegisterMetaType<VideoFrame>("QtScrcpy::VideoFrame");
    qRegisterMetaType<State>("QtScrcpy::ScrcpyStream::State");
    // Queued behind the frame signal of the same emission, so the count goes down once it was delivered.
    connect(this, &ScrcpyStream::frameReady, this, [this] { --m_pending; }, Qt::QueuedConnection);
}

ScrcpyStream::~ScrcpyStream()
{
    stop();
    if (m_thread) {
        m_thread->wait(10000);
        delete m_thread;
    }
}

QString ScrcpyStream::findServerJar()
{
    const QString fromEnv = qEnvironmentVariable("SCRCPY_SERVER_PATH");
    if (!fromEnv.isEmpty() && QFileInfo(fromEnv).isFile())
        return fromEnv;

    QStringList dirs;
    if (QCoreApplication::instance())
        dirs << QCoreApplication::applicationDirPath();
    const QString scrcpy = resolveExecutable(QStringLiteral("scrcpy"));
    if (!scrcpy.isEmpty())
        dirs << QFileInfo(scrcpy).absolutePath();
    for (const QString &dir : dirs) {
        const QString candidate = QDir(dir).filePath(QStringLiteral("scrcpy-server"));
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return {};
}

QString ScrcpyStream::serverCommand(const Options &options, const QString &version, quint32 scid)
{
    QStringList args;
    args << QString::fromLatin1(kDeviceJar).prepend(QStringLiteral("CLASSPATH=")) << QStringLiteral("app_process")
         << QStringLiteral("/") << QStringLiteral("com.genymobile.scrcpy.Server") << version
         << QStringLiteral("scid=%1").arg(scid, 8, 16, QLatin1Char('0')) << QStringLiteral("log_level=info")
         << QStringLiteral("audio=false") << QStringLiteral("control=false")
         << QStringLiteral("tunnel_forward=true") << QStringLiteral("cleanup=false");
    if (options.camera != Camera::None) {
        args << QStringLiteral("video_source=camera");
        switch (options.camera) {
        case Camera::Back:
            args << QStringLiteral("camera_facing=back");
            break;
        case Camera::Front:
            args << QStringLiteral("camera_facing=front");
            break;
        case Camera::External:
            args << QStringLiteral("camera_facing=external");
            break;
        case Camera::None:
            break;
        }
    }
    if (options.maxSize > 0)
        args << QStringLiteral("max_size=%1").arg(options.maxSize);
    if (options.bitRate > 0)
        args << QStringLiteral("video_bit_rate=%1").arg(options.bitRate);
    if (options.maxFps > 0)
        args << QStringLiteral("max_fps=%1").arg(options.maxFps);
    return args.join(QLatin1Char(' '));
}

void ScrcpyStream::setState(State state)
{
    if (m_state.exchange(int(state)) != int(state))
        emit stateChanged(state);
}

void ScrcpyStream::start()
{
    if (m_state.load() != int(State::Stopped))
        return;
    if (m_thread) {
        m_thread->wait(10000);
        delete m_thread;
        m_thread = nullptr;
    }
    m_stop = false;
    m_pending = 0;
    setState(State::Starting);
    m_thread = QThread::create([this] { run(); });
    m_thread->start();
}

void ScrcpyStream::stop()
{
    m_stop = true;
}

void ScrcpyStream::run()
{
    QString error;
    QString forwarded; // serial + port of a forward that has to be removed
    int port = 0;
    AdbClient client(m_adb);

    const auto stopped = [this] { return m_stop.load(); };
    const auto finish = [&](const QString &message) {
        if (port > 0)
            client.removeForward(m_options.serial, port);
        setState(State::Stopped);
        emit finished(m_stop ? QString() : message);
    };

    if (m_jar.isEmpty() || !QFileInfo(m_jar).isFile()) {
        finish(tr("找不到 scrcpy-server 文件（应与程序放在同一目录，或设置环境变量 SCRCPY_SERVER_PATH）。"));
        return;
    }
    if (!client.ensureServer(&error)) {
        finish(tr("无法连接 adb 服务：%1").arg(error));
        return;
    }
    if (stopped()) {
        finish({});
        return;
    }
    if (!client.push(m_options.serial, m_jar, QString::fromLatin1(kDeviceJar), &error)) {
        finish(tr("无法把 scrcpy-server 传到手机：%1").arg(error));
        return;
    }

    const quint32 scid = QRandomGenerator::global()->bounded(0x7fffffffu);
    port = findFreePort();
    if (port <= 0) {
        finish(tr("没有可用的本机端口。"));
        return;
    }
    const QString socketName = QStringLiteral("scrcpy_%1").arg(scid, 8, 16, QLatin1Char('0'));
    if (!client.forward(m_options.serial, port, QStringLiteral("localabstract:") + socketName, &error)) {
        port = 0;
        finish(tr("端口转发失败：%1").arg(error));
        return;
    }

    // The server stays alive as long as this shell connection does; its output explains failures.
    std::unique_ptr<QTcpSocket> shell = client.shell(m_options.serial, serverCommand(m_options, m_serverVersion, scid), &error);
    if (!shell) {
        finish(tr("无法在手机上启动 scrcpy-server：%1").arg(error));
        return;
    }
    ServerLog log;

    // The server listens only after the JVM is up: keep trying until it hands out the dummy byte.
    QTcpSocket video;
    bool connected = false;
    for (int attempt = 0; attempt < kConnectAttempts && !stopped(); ++attempt) {
        log.pump(shell.get());
        if (shell->state() != QAbstractSocket::ConnectedState && shell->bytesAvailable() == 0 && attempt > 3) {
            finish(tr("手机上的 scrcpy-server 退出了：%1").arg(log.lastProblem()));
            return;
        }
        video.abort();
        video.connectToHost(QHostAddress::LocalHost, quint16(port));
        if (!video.waitForConnected(1000)) {
            QThread::msleep(100);
            continue;
        }
        if (video.waitForReadyRead(300) && video.bytesAvailable() >= 1) {
            video.read(1); // dummy byte
            connected = true;
            break;
        }
        if (video.state() == QAbstractSocket::ConnectedState)
            video.waitForReadyRead(200);
        if (video.bytesAvailable() >= 1) {
            video.read(1);
            connected = true;
            break;
        }
        QThread::msleep(100);
    }
    if (stopped()) {
        finish({});
        return;
    }
    if (!connected) {
        log.pump(shell.get());
        finish(tr("连接不上手机上的 scrcpy-server：%1").arg(log.lastProblem()));
        return;
    }
    video.setSocketOption(QAbstractSocket::LowDelayOption, 1);

    QByteArray buffer;
    enum class Phase { DeviceName, CodecMeta, Packets } phase = Phase::DeviceName;
    H264Decoder decoder;
    if (!decoder.isUsable()) {
        finish(decoder.errorString());
        return;
    }
    QByteArray config;
    setState(State::Streaming);

    while (!stopped()) {
        log.pump(shell.get());
        if (video.bytesAvailable() == 0 && !video.waitForReadyRead(200)) {
            if (video.state() != QAbstractSocket::ConnectedState) {
                finish(tr("手机端已断开：%1").arg(log.lastProblem()));
                return;
            }
            continue;
        }
        buffer += video.readAll();

        while (!stopped()) {
            if (phase == Phase::DeviceName) {
                if (buffer.size() < kDeviceNameFieldLength)
                    break;
                buffer.remove(0, kDeviceNameFieldLength);
                phase = Phase::CodecMeta;
                continue;
            }
            if (phase == Phase::CodecMeta) {
                if (buffer.size() < 12)
                    break;
                const auto *p = reinterpret_cast<const uchar *>(buffer.constData());
                const quint32 codec = be32(p);
                if (codec == 0 || codec == 1) {
                    finish(tr("手机端拒绝了这次投屏：%1").arg(log.lastProblem()));
                    return;
                }
                if (codec != quint32(kCodecH264)) {
                    finish(tr("手机端使用了不支持的视频编码 0x%1。").arg(codec, 8, 16, QLatin1Char('0')));
                    return;
                }
                buffer.remove(0, 12);
                phase = Phase::Packets;
                continue;
            }

            if (buffer.size() < 12)
                break;
            const auto *p = reinterpret_cast<const uchar *>(buffer.constData());
            const quint64 ptsFlags = (quint64(be32(p)) << 32) | be32(p + 4);
            const quint32 size = be32(p + 8);
            if (size > kMaxPacket) {
                finish(tr("画面数据无效（数据包 %1 字节）。").arg(size));
                return;
            }
            if (quint32(buffer.size()) < 12 + size)
                break;
            QByteArray payload = buffer.mid(12, int(size));
            buffer.remove(0, 12 + int(size));

            if (ptsFlags & kFlagConfig) {
                config = payload; // goes in front of the next picture
                continue;
            }
            if (!config.isEmpty()) {
                payload.prepend(config);
                config.clear();
            }
            const QList<VideoFrame> frames = decoder.decode(payload);
            for (const VideoFrame &frame : frames) {
                if (m_pending.load() >= kMaxPendingFrames)
                    continue; // the receiver is behind: show the newest picture, not a backlog
                ++m_pending;
                emit frameReady(frame);
            }
        }
    }
    finish({});
}

} // namespace QtScrcpy
