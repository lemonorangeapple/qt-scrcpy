#include "hdcstream.h"
#include "hapsigner.h"
#include "imagedecoder.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <memory>

namespace QtScrcpy {

namespace {

constexpr int kRetryIntervalMs = 500;

// The device-side command prints its complaint on stdout, which is what the user needs to see.
QString theDeviceMessage(const CommandResult &shot, const CommandResult &recv)
{
    const QString text = (shot.text() + QLatin1Char('\n') + recv.text()).trimmed();
    return text.isEmpty() ? QStringLiteral("设备没有返回画面") : text;
}

int findFreePort()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return 0;
    return server.serverPort();
}

} // namespace

HdcStream::HdcStream(Hdc *hdc, const QString &serial, StreamProtocol::Source source, QObject *parent)
    : QObject(parent)
    , m_hdc(hdc)
    , m_serial(serial)
    , m_source(source)
{
    qRegisterMetaType<VideoFrame>("QtScrcpy::VideoFrame");
    qRegisterMetaType<State>("QtScrcpy::HdcStream::State");

    m_retryTimer.setInterval(kRetryIntervalMs);
    connect(&m_retryTimer, &QTimer::timeout, this, &HdcStream::tryConnect);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        fail(tr("等待手机上的“%1”应用超时：请在手机上允许录屏/相机权限，并确认已安装配套应用。")
                 .arg(QString::fromLatin1(kBundleName)));
    });

    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_retryTimer.stop();
        m_deadline.stop();
        setState(State::Streaming);
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &HdcStream::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        if (m_state == State::Streaming)
            fail(m_parser.hasHeader() ? tr("手机端已断开连接。") : tr("手机端没有发送画面就断开了连接。"));
    });
}

HdcStream::~HdcStream()
{
    teardown();
}

QString HdcStream::sourceName(StreamProtocol::Source source)
{
    switch (source) {
    case StreamProtocol::Source::Screen:
        return QStringLiteral("screen");
    case StreamProtocol::Source::BackCamera:
        return QStringLiteral("back");
    case StreamProtocol::Source::FrontCamera:
        return QStringLiteral("front");
    }
    return QStringLiteral("screen");
}

QStringList HdcStream::launchArguments(const QString &serial, StreamProtocol::Source source)
{
    return {QStringLiteral("-t"), serial, QStringLiteral("shell"), QStringLiteral("aa"), QStringLiteral("start"),
            QStringLiteral("-a"), QStringLiteral("EntryAbility"), QStringLiteral("-b"),
            QString::fromLatin1(kBundleName), QStringLiteral("--ps"), QStringLiteral("source"), sourceName(source)};
}

QString HdcStream::findHapToInstall(bool *unsignedOut)
{
    QStringList names{QStringLiteral("qtscrcpy-harmony-unsigned.hap"), QStringLiteral("qtscrcpy-harmony.hap"),
                      QStringLiteral("streamer-unsigned.hap"), QStringLiteral("streamer.hap")};
    QStringList dirs;
    if (QCoreApplication::instance())
        dirs << QCoreApplication::applicationDirPath();
    const QString fromEnv = qEnvironmentVariable("QTSCRCPY_HAP");
    QStringList candidates;
    if (!fromEnv.isEmpty())
        candidates.prepend(fromEnv);
    for (const QString &dir : dirs) {
        for (const QString &name : names)
            candidates << QDir(dir).filePath(name);
    }
    for (const QString &candidate : candidates) {
        if (!QFileInfo(candidate).isFile())
            continue;
        if (unsignedOut)
            *unsignedOut = candidate.contains(QLatin1String("unsigned"), Qt::CaseInsensitive);
        return QDir::toNativeSeparators(candidate);
    }
    return {};
}

void HdcStream::ensureAppInstalled(std::function<void()> then)
{
    if (m_hap.isEmpty()) {
        // Nothing was named: use the package that sits next to the program, if there is one. That is what
        // makes "plug the phone in and press start" work without any configuration.
        bool isUnsigned = false;
        const QString found = findHapToInstall(&isUnsigned);
        if (found.isEmpty()) {
            then();
            return;
        }
        m_hap = found;
        m_hapUnsigned = isUnsigned;
    }

    const QPointer<HdcStream> self(this);
    const QString serial = m_serial;
    const QString hap = m_hap;

    // Already there (any version): nothing to do.
    if (m_hdc->isBundleInstalled(serial, QString::fromLatin1(kBundleName))) {
        then();
        return;
    }

    // Everything that can go wrong here ends in the screenshot fallback when that is possible: a device
    // that refuses our signature can still show its screen through the system screenshot command.
    const auto giveUp = [this](const QString &reason) {
        if (startFallback(reason))
            return;
        fail(reason);
    };

    const auto declined = [this] { return tr("已选择不安装配套应用。"); };

    const auto install = [this, self, serial, then, giveUp](const QString &signedHap) {
        m_hdc->installAsync(serial, signedHap, [this, self, signedHap, then, giveUp](bool ok, const QString &error) {
            if (!self || m_state != State::Starting)
                return;
            if (!ok) {
                giveUp(tr("手机拒绝安装配套应用（%1）：%2\n"                          "零售版 HarmonyOS 只接受华为签发的证书，这种设备需要自己用 DevEco Studio 签名后安装一次。")
                           .arg(signedHap, error.trimmed()));
                return;
            }
            then();
        });
    };

    const auto proceed = [this, self, hap, install, giveUp]() {
        if (!m_hapUnsigned) {
            install(hap);
            return;
        }
        signAndInstall(hap, install, giveUp);
    };

    if (!m_confirmInstall) {
        proceed();
        return;
    }
    m_confirmInstall(serial, [this, self, proceed, giveUp, declined](bool accepted) {
        if (!self || m_state != State::Starting)
            return;
        if (accepted)
            proceed();
        else
            giveUp(declined());
    });
}

void HdcStream::signAndInstall(const QString &hap, std::function<void(const QString &)> install,
                               std::function<void(const QString &)> giveUp)
{
    const QPointer<HdcStream> self(this);
    const QString serial = m_serial;
    const auto signer = std::make_shared<HapSigner>(m_signingDirectory);
    m_hdc->deviceUdidAsync(serial, [this, self, signer, hap, install, giveUp](const QString &udid) {
        if (!self || m_state != State::Starting)
            return;
        if (udid.isEmpty()) {
            giveUp(tr("读不到手机的 UDID，无法为它签名（需要在开发者选项里打开 USB 调试）。"));
            return;
        }
        const QString problem = signer->checkReady();
        if (!problem.isEmpty()) {
            giveUp(problem);
            return;
        }
        const QString signedHap = QDir(QDir::tempPath()).filePath(
            QStringLiteral("qtscrcpy-%1.hap").arg(QFileInfo(hap).completeBaseName()));
        QString error;
        if (!signer->signForDevice(udid, hap, signedHap, &error)) {
            giveUp(tr("给设备签名失败：%1").arg(error.trimmed()));
            return;
        }
        install(signedHap);
    });
}

bool HdcStream::startFallback(const QString &why)
{
    if (!m_fallback || m_source != StreamProtocol::Source::Screen) {
        // A camera cannot be read with a screenshot command, so the reason must reach the user.
        return false;
    }
    m_mode = Mode::Screenshots;
    emit notice(tr("%1\n改用系统截图方式投屏：只有屏幕、每秒几帧。").arg(why));
    setState(State::Streaming);
    startScreenshots();
    return true;
}

void HdcStream::startScreenshots()
{
    m_failures = 0;
    takeSnapshot();
}

void HdcStream::takeSnapshot()
{
    if (m_state == State::Stopped || m_snapshotBusy)
        return;
    m_snapshotBusy = true;

    // The picture is written on the device (snapshot_display) and then fetched; both steps go through hdc.
    const QPointer<HdcStream> self(this);
    const QString remote = QStringLiteral("/data/local/tmp/qtscrcpy_snapshot.jpeg");
    const QString local = QDir::toNativeSeparators(
        QDir(QDir::tempPath()).filePath(QStringLiteral("qtscrcpy-snapshot-%1.jpeg")
                                            .arg(QString(m_serial).remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]"))))));
    QFile::remove(local);

    runHdc({QStringLiteral("-t"), m_serial, QStringLiteral("shell"), QStringLiteral("snapshot_display"),
            QStringLiteral("-f"), remote},
           [this, self, remote, local](const CommandResult &shot) {
               if (!self || m_state == State::Stopped)
                   return;
               runHdc({QStringLiteral("-t"), m_serial, QStringLiteral("file"), QStringLiteral("recv"), remote, local},
                      [this, self, local, shot](const CommandResult &recv) {
                          if (!self || m_state == State::Stopped)
                              return;
                          m_snapshotBusy = false;

                          QFile file(local);
                          bool ok = false;
                          if (file.open(QIODevice::ReadOnly)) {
                              const QByteArray data = file.readAll();
                              if (!data.isEmpty()) {
                                  static ImageDecoder decoder;
                                  const VideoFrame frame = decoder.decode(data);
                                  if (frame.width > 0) {
                                      emit frameReady(frame);
                                      ok = true;
                                  }
                              }
                          }
                          file.remove();

                          if (ok) {
                              m_failures = 0;
                              // Give the device and the USB link a moment; without this the loop saturates
                              // the connection and the frame rate drops instead of rising.
                              QTimer::singleShot(60, this, &HdcStream::takeSnapshot);
                              return;
                          }
                          if (++m_failures >= 3) {
                              const QString reason = !recv.ok() ? recv.failureReason()
                                                     : !shot.ok() ? shot.failureReason()
                                                                  : theDeviceMessage(shot, recv);
                              fail(tr("截图投屏失败：%1").arg(reason));
                              return;
                          }
                          QTimer::singleShot(300, this, &HdcStream::takeSnapshot);
                      });
           });
}

void HdcStream::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void HdcStream::start()
{
    if (m_state != State::Stopped)
        return;
    m_finishedEmitted = false;
    m_config.clear();
    m_parser = StreamProtocol::Parser();
    m_codec = 0;
    m_jpeg.reset();
    m_decoder = std::make_unique<H264Decoder>();

    if (!m_decoder->isUsable()) {
        fail(m_decoder->errorString());
        return;
    }
    if (!m_hdc->isAvailable()) {
        fail(tr("未找到 hdc。"));
        return;
    }
    setState(State::Starting);

    // Install the companion application first when it is missing, then restart it from a clean state.
    const QPointer<HdcStream> self(this);
    ensureAppInstalled([this, self] {
        if (!self || m_state != State::Starting)
            return;
        runHdc({QStringLiteral("-t"), m_serial, QStringLiteral("shell"), QStringLiteral("aa"),
                QStringLiteral("force-stop"), QString::fromLatin1(kBundleName)},
               [this, self](const CommandResult &) {
                   if (!self || m_state != State::Starting)
                       return;
                   runHdc(launchArguments(m_serial, m_source), [this, self](const CommandResult &launch) {
                   if (!self || m_state != State::Starting)
                       return;
                   // `aa start` prints its verdict and may still exit 0.
                   const QString output = launch.text();
                   if (!launch.ok() || output.contains(QLatin1String("error"), Qt::CaseInsensitive)
                       || output.contains(QLatin1String("fail"), Qt::CaseInsensitive)) {
                       const QString message = tr("无法启动手机上的配套应用（%1）：%2")
                                                   .arg(QString::fromLatin1(kBundleName),
                                                        launch.ok() ? output : launch.failureReason());
                       if (startFallback(message))
                           return;
                       fail(message);
                       return;
                   }
                       forwardPort();
                   });
               });
    });
}

void HdcStream::forwardPort()
{
    m_forwardedPort = m_localPort > 0 ? m_localPort : findFreePort();
    if (m_forwardedPort <= 0) {
        fail(tr("没有可用的本机端口。"));
        return;
    }
    const QPointer<HdcStream> self(this);
    runHdc({QStringLiteral("-t"), m_serial, QStringLiteral("fport"),
            QStringLiteral("tcp:%1").arg(m_forwardedPort),
            QStringLiteral("tcp:%1").arg(StreamProtocol::kDefaultDevicePort)},
           [this, self](const CommandResult &result) {
               if (!self || m_state != State::Starting)
                   return;
               // hdc exits 0 with "[Fail]..." when the forward is refused.
               const QString output = result.text();
               if (!result.ok() || output.contains(QLatin1String("[Fail]"))) {
                   m_forwardedPort = 0;
                   const QString message = tr("端口转发失败：%1").arg(result.ok() ? output : result.failureReason());
                   if (startFallback(message))
                       return;
                   fail(message);
                   return;
               }
               setState(State::Waiting);
               m_deadline.start(m_connectTimeoutMs);
               m_retryTimer.start();
               tryConnect();
           });
}

void HdcStream::tryConnect()
{
    if (m_state != State::Waiting)
        return;
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        return; // an attempt is already running
    m_socket.connectToHost(QHostAddress::LocalHost, quint16(m_forwardedPort));
}

void HdcStream::onReadyRead()
{
    m_parser.append(m_socket.readAll());

    while (true) {
        StreamProtocol::Header header;
        StreamProtocol::Packet packet;
        const auto status = m_parser.next(&header, &packet);
        if (status == StreamProtocol::Parser::Status::NeedMore)
            return;
        if (status == StreamProtocol::Parser::Status::Error) {
            fail(tr("画面数据无效：%1").arg(m_parser.errorString()));
            return;
        }
        if (status == StreamProtocol::Parser::Status::Header) {
            if (header.codec != m_codec) {
                m_codec = header.codec;
                if (m_codec == 2) {
                    m_jpeg = std::make_unique<ImageDecoder>();
                    if (!m_jpeg->isUsable()) {
                        fail(m_jpeg->errorString());
                        return;
                    }
                } else {
                    m_decoder = std::make_unique<H264Decoder>(m_codec == 1 ? H264Decoder::Codec::H265
                                                                           : H264Decoder::Codec::H264);
                    if (!m_decoder->isUsable()) {
                        fail(m_decoder->errorString());
                        return;
                    }
                }
            }
            continue;
        }

        if (m_codec == 2) {
            // Motion JPEG: every packet is a complete picture.
            const VideoFrame frame = m_jpeg ? m_jpeg->decode(packet.payload) : VideoFrame();
            if (frame.width > 0)
                emit frameReady(frame);
            continue;
        }
        if (packet.type == StreamProtocol::PacketType::Config) {
            m_config = packet.payload;
            const QList<VideoFrame> frames = m_decoder->decode(m_config);
            for (const VideoFrame &frame : frames)
                emit frameReady(frame);
            continue;
        }
        // A key frame must be preceded by the parameter sets, even if the stream repeats them.
        const QByteArray data = packet.type == StreamProtocol::PacketType::KeyFrame && !m_config.isEmpty()
                                    ? m_config + packet.payload
                                    : packet.payload;
        const QList<VideoFrame> frames = m_decoder->decode(data);
        for (const VideoFrame &frame : frames)
            emit frameReady(frame);
    }
}

void HdcStream::runHdc(const QStringList &args, std::function<void(const CommandResult &)> callback)
{
    m_hdc->runAsync(args, std::move(callback));
}

void HdcStream::fail(const QString &message)
{
    teardown();
    if (!m_finishedEmitted) {
        m_finishedEmitted = true;
        emit finished(message);
    }
}

void HdcStream::stop()
{
    if (m_state == State::Stopped && m_finishedEmitted)
        return;
    teardown();
    if (!m_finishedEmitted) {
        m_finishedEmitted = true;
        emit finished({});
    }
}

void HdcStream::teardown()
{
    m_snapshotBusy = false; // a queued snapshot must not restart the loop after a stop
    m_retryTimer.stop();
    m_deadline.stop();
    m_socket.abort();
    if (m_forwardedPort > 0) {
        // Fire and forget: the forward and the app are only useful while this session lives.
        m_hdc->runAsync({QStringLiteral("-t"), m_serial, QStringLiteral("fport"), QStringLiteral("rm"),
                         QStringLiteral("tcp:%1").arg(m_forwardedPort),
                         QStringLiteral("tcp:%1").arg(StreamProtocol::kDefaultDevicePort)},
                        nullptr);
        m_forwardedPort = 0;
    }
    if (m_state != State::Stopped) {
        m_hdc->runAsync({QStringLiteral("-t"), m_serial, QStringLiteral("shell"), QStringLiteral("aa"),
                         QStringLiteral("force-stop"), QString::fromLatin1(kBundleName)},
                        nullptr);
    }
    setState(State::Stopped);
}

} // namespace QtScrcpy
