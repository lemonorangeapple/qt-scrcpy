#pragma once

#include "h264decoder.h"
#include "hdc.h"
#include "imagedecoder.h"
#include "qtscrcpy/qtscrcpy_export.h"
#include "streamprotocol.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <memory>

namespace QtScrcpy {

// A live video session with the HarmonyOS streamer app (harmony/) on one device:
// starts the app through hdc, forwards its port, connects, parses and decodes the H.264 stream.
class QTSCRCPY_EXPORT HdcStream : public QObject
{
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Waiting, Streaming };
    // How the picture is obtained: the streamer app (real video) or, when that is impossible, the
    // system screenshot command run over and over (a few frames per second, screen only).
    enum class Mode { Streamer, Screenshots };
    Q_ENUM(State)

    static constexpr const char *kBundleName = "com.qtscrcpy.streamer";

    HdcStream(Hdc *hdc, const QString &serial, StreamProtocol::Source source, QObject *parent = nullptr);
    ~HdcStream() override;

    State state() const { return m_state; }
    Mode mode() const { return m_mode; }
    // Ask for the screenshot fallback instead of giving up when the app cannot be installed or started.
    void setFallbackToScreenshots(bool enabled) { m_fallback = enabled; }
    QString serial() const { return m_serial; }
    StreamProtocol::Source source() const { return m_source; }

    // `hdc shell aa start ...` arguments that launch the app for `source`.
    static QStringList launchArguments(const QString &serial, StreamProtocol::Source source);
    static QString sourceName(StreamProtocol::Source source);

    // A HAP to install when the app is not on the device yet: an unsigned one is signed for this device
    // first (needs Java and the signing materials), an already signed one is installed as it is.
    // Empty = never install anything, the app must already be there.
    void setHapToInstall(const QString &hapFile, bool isUnsigned) { m_hap = hapFile; m_hapUnsigned = isUnsigned; }
    // Asked before the app is installed on a device that lacks it; `answer(true)` installs, `answer(false)`
    // skips the install (screenshots are used instead). Without a handler the app is installed unasked.
    using InstallConfirmation = std::function<void(const QString &serial, std::function<void(bool)> answer)>;
    void setInstallConfirmation(InstallConfirmation handler) { m_confirmInstall = std::move(handler); }
    // Where the signing materials are; empty = let HapSigner find them.
    void setSigningDirectory(const QString &directory) { m_signingDirectory = directory; }

    // The HAP that should be installed for `serial`, looking next to the application. Empty when there
    // is none. `unsignedOut` says whether it still has to be signed.
    static QString findHapToInstall(bool *unsignedOut);

    // How long to wait for the user to accept the permission prompts on the phone.
    void setConnectTimeoutMs(int ms) { m_connectTimeoutMs = ms; }
    // Port on this computer that is forwarded to the device (0 = pick a free one).
    void setLocalPort(int port) { m_localPort = port; }

    void start();
    void stop();

signals:
    void stateChanged(QtScrcpy::HdcStream::State state);
    // One line for the user: which mode was chosen, or why the preferred one was dropped.
    void notice(const QString &text);
    void frameReady(const QtScrcpy::VideoFrame &frame);
    // Emitted once when the stream ended or could not be set up. Empty text means a normal stop.
    void finished(const QString &error);

private:
    void setState(State state);
    void fail(const QString &message);
    void forwardPort();
    void tryConnect();
    void onReadyRead();
    void teardown();
    void runHdc(const QStringList &args, std::function<void(const CommandResult &)> callback);
    // Falls back to the screenshot loop, if that is allowed and the source makes sense for it.
    bool startFallback(const QString &why);
    void startScreenshots();
    void takeSnapshot();
    // Makes sure the companion app is on the device (installing it when a HAP is available), then
    // continues with `then`.
    void ensureAppInstalled(std::function<void()> then);
    void signAndInstall(const QString &hap, std::function<void(const QString &)> install,
                        std::function<void(const QString &)> giveUp);

    Hdc *m_hdc;
    QString m_serial;
    StreamProtocol::Source m_source;
    State m_state = State::Stopped;
    int m_connectTimeoutMs = 120 * 1000;
    int m_localPort = 0;
    int m_forwardedPort = 0;

    QTcpSocket m_socket;
    QTimer m_retryTimer;
    QTimer m_deadline;
    StreamProtocol::Parser m_parser;
    std::unique_ptr<H264Decoder> m_decoder = std::make_unique<H264Decoder>();
    quint8 m_codec = 0;
    std::unique_ptr<ImageDecoder> m_jpeg;
    QByteArray m_config; // latest SPS/PPS
    bool m_finishedEmitted = false;
    QString m_hap;
    bool m_hapUnsigned = false;
    QString m_signingDirectory;
    InstallConfirmation m_confirmInstall;
    Mode m_mode = Mode::Streamer;
    bool m_fallback = true;
    bool m_snapshotBusy = false;
    int m_failures = 0;
};

} // namespace QtScrcpy
