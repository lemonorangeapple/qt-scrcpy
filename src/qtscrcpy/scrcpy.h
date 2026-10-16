#pragma once

#include "qtscrcpy/qtscrcpy_export.h"
#include "scrcpyoptions.h"

#include <QObject>
#include <QProcess>
#include <QTimer>

namespace QtScrcpy {

// Runs one scrcpy instance (one mirrored device) as a child process.
//
// scrcpy opens its own window, so this class manages the process and its
// lifecycle/log output rather than embedding the video. Create one Scrcpy
// per device to mirror several devices at once.
class QTSCRCPY_EXPORT Scrcpy : public QObject
{
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Running, Stopping };
    Q_ENUM(State)

    explicit Scrcpy(QObject *parent = nullptr);
    // Stops a still-running scrcpy.
    ~Scrcpy() override;

    // Looks in $SCRCPY_EXECUTABLE, next to the application and $PATH. Empty if not found.
    static QString findExecutable();

    QString executable() const { return m_executable; }
    void setExecutable(const QString &executable);
    bool isAvailable() const;

    // adb binary scrcpy should use (exported to it as $ADB). Empty keeps scrcpy's own lookup.
    QString adbExecutable() const { return m_adbExecutable; }
    void setAdbExecutable(const QString &adb) { m_adbExecutable = adb; }

    // Version reported by `scrcpy --version` (cached; invalid if scrcpy cannot be run).
    ScrcpyVersion version() const;

    State state() const { return m_state; }
    bool isRunning() const { return m_state == State::Starting || m_state == State::Running; }
    qint64 processId() const;
    // Arguments of the current/last run.
    QStringList arguments() const { return m_arguments; }

    // Launches scrcpy. Returns false (and emits errorOccurred) when scrcpy is already
    // running, its executable is missing or the options do not fit this scrcpy version
    // (ScrcpyOptions::validate()); a launch that fails later is reported
    // through errorOccurred only. Detects the scrcpy version first, which briefly
    // blocks the first time.
    bool start(const ScrcpyOptions &options);
    // Asks scrcpy to quit (SIGTERM / WM_CLOSE) and kills it after `gracePeriodMs`.
    void stop(int gracePeriodMs = 3000);

signals:
    void stateChanged(QtScrcpy::Scrcpy::State state);
    // Emitted when the process has been launched.
    void started();
    // Emitted when the process exited, whether by user request, crash or because the
    // device went away.
    void finished(int exitCode, bool crashed);
    void errorOccurred(const QString &message);
    // One line of scrcpy output (stdout and stderr).
    void outputReceived(const QString &line, bool isError);

private:
    void setState(State state);
    void readOutput(bool isError);
    void flushOutput(bool isError);
    void handleFinished(int exitCode, QProcess::ExitStatus status);

    QString m_executable;
    QString m_adbExecutable;
    mutable ScrcpyVersion m_version;
    mutable QString m_versionOf; // executable m_version was read from

    QProcess m_process;
    QTimer m_killTimer;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QStringList m_arguments;
    State m_state = State::Stopped;
    bool m_stopRequested = false;
};

} // namespace QtScrcpy
