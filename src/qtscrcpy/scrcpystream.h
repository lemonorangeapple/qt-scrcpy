#pragma once

#include "h264decoder.h"
#include "qtscrcpy/qtscrcpy_export.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QThread;

namespace QtScrcpy {

// In-process scrcpy client: no scrcpy.exe. It pushes `scrcpy-server` to an Android device through the adb
// server, starts it, receives the H.264 video and decodes it. Screen mirroring and camera mirroring,
// view only (no control channel) and no audio.
//
// Everything (setup, network, decoding) runs on a worker thread; signals are delivered to the thread of
// their receivers. If a receiver is slow, frames are dropped instead of queued.
class QTSCRCPY_EXPORT ScrcpyStream : public QObject
{
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Streaming };
    Q_ENUM(State)

    enum class Camera { None, Back, Front, External };

    struct Options {
        QString serial;
        Camera camera = Camera::None; // None = mirror the screen
        int maxSize = 0;              // limit width/height (0 = device resolution)
        int bitRate = 0;              // bits per second (0 = scrcpy's default)
        int maxFps = 0;               // 0 = unlimited
    };

    // The scrcpy release this library's client protocol and bundled server belong to.
    static constexpr const char *kBundledServerVersion = "3.3.3";

    // `adbExecutable` is used to start the adb server when it is not running (empty = Adb::findExecutable()).
    // `serverJar` is the `scrcpy-server` file (empty = findServerJar()).
    explicit ScrcpyStream(const Options &options, const QString &adbExecutable = {}, const QString &serverJar = {},
                          QObject *parent = nullptr);
    ~ScrcpyStream() override;

    // $SCRCPY_SERVER_PATH, then `scrcpy-server` next to the application, then next to `scrcpy` on $PATH.
    static QString findServerJar();

    // Must equal the version of the scrcpy-server file: the server refuses a client of another version.
    void setServerVersion(const QString &version) { m_serverVersion = version; }

    State state() const { return State(m_state.load()); }
    QString serial() const { return m_options.serial; }

    // Builds the `app_process` command line that starts the server (exposed for tests).
    static QString serverCommand(const Options &options, const QString &version, quint32 scid);

    void start();
    // Returns immediately; finished() follows.
    void stop();

signals:
    void stateChanged(QtScrcpy::ScrcpyStream::State state);
    void frameReady(const QtScrcpy::VideoFrame &frame);
    // Emitted once per start(); empty text means a normal stop.
    void finished(const QString &error);

private:
    void run();
    void setState(State state);

    Options m_options;
    QString m_adb;
    QString m_jar;
    QString m_serverVersion = QString::fromLatin1(kBundledServerVersion);

    std::atomic<int> m_state{int(State::Stopped)};
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_pending{0}; // frames emitted but not yet delivered
    QThread *m_thread = nullptr;
};

} // namespace QtScrcpy
