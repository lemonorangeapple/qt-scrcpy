#pragma once

#include "adbdevice.h"
#include "h264decoder.h"
#include "qtscrcpy/qtscrcpy_export.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace QtScrcpy {

class Hdc;

// Which tool reaches the device: adb (Android, and HarmonyOS with the Android layer) or hdc (HarmonyOS NEXT).
enum class Backend { Adb, Hdc };

enum class MirrorSource {
    Screen,
    BackCamera,
    FrontCamera,
    ExternalCamera, // adb only
};

struct QTSCRCPY_EXPORT MirrorOptions
{
    Backend backend = Backend::Adb;
    QString serial;
    MirrorSource source = MirrorSource::Screen;

    // Android only (the HarmonyOS app picks its own values); 0 = default.
    int maxSize = 0;
    int bitRate = 0;
    int maxFps = 0;

    // HarmonyOS only: a HAP to install on the device when the companion application is missing. An
    // unsigned one is signed for the connected device first (that is the usual choice); leave empty to
    // use the package found next to the program, and both empty to never install anything.
    QString hapToInstall;
    bool hapIsUnsigned = true;
};

// One live, view-only video session with a device, whatever the backend: the same calls and signals
// for adb and hdc. Frames are decoded BGRA pictures (see VideoFrame).
class QTSCRCPY_EXPORT MirrorSession : public QObject
{
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Streaming };
    Q_ENUM(State)

    // `hdc` must outlive the session and is required for Backend::Hdc. `adbExecutable` is only used to start
    // the adb server (empty = look for it). Returns a session that fails at start() with a readable message
    // when the combination is not possible (e.g. an external camera over hdc).
    MirrorSession(const MirrorOptions &options, Hdc *hdc = nullptr, const QString &adbExecutable = {},
                  QObject *parent = nullptr);
    ~MirrorSession() override;

    const MirrorOptions &options() const { return m_options; }
    State state() const { return m_state; }

    // Reason a source cannot be used with a backend, empty when it can.
    static QString unsupportedReason(Backend backend, MirrorSource source);

    void start();
    // Asks the session to end; finished() follows.
    void stop();

signals:
    void stateChanged(QtScrcpy::MirrorSession::State state);
    void frameReady(const QtScrcpy::VideoFrame &frame);
    // One line about how the picture is obtained (e.g. that it fell back to screenshots), for the user.
    void notice(const QString &text);
    // HarmonyOS only: the companion app is missing on `serial`. Call answer(true) to install it (signing
    // the HAP for that device) or answer(false) to use screenshots instead. Without a receiver connected
    // the app is installed unasked.
    void installRequested(const QString &serial, std::function<void(bool)> answer);
    // Emitted once per start(); empty text means it ended because stop() was called.
    void finished(const QString &error);

private:
    void setState(State state);

    MirrorOptions m_options;
    State m_state = State::Stopped;
    bool m_finishedEmitted = false;
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace QtScrcpy
