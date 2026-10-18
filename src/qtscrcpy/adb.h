#pragma once

#include "adbdevice.h"
#include "commandresult.h"
#include "qtscrcpy/qtscrcpy_export.h"

#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

class QProcess;

namespace QtScrcpy {

// Qt wrapper around the part of the `adb` command line client this project needs:
// running adb, reading the device list and watching it for changes.
class QTSCRCPY_EXPORT Adb : public QObject
{
    Q_OBJECT
public:
    static constexpr int kDefaultTimeoutMs = 15 * 1000;
    // Reading the device list may include starting the adb server, which on a cold Windows machine
    // (driver installation, antivirus scanning adb.exe) can take much longer than a normal command.
    static constexpr int kDeviceListTimeoutMs = 60 * 1000;
    static constexpr int kServerStartTimeoutMs = 120 * 1000;
    // Every timeout can be raised from the environment for machines that need even longer.
    static int timeoutFromEnvironment(const char *variable, int fallbackMs);

    using Callback = std::function<void(const CommandResult &)>;

    // Uses findExecutable(), falling back to plain "adb" (resolved through $PATH on start).
    explicit Adb(QObject *parent = nullptr);
    explicit Adb(const QString &executable, QObject *parent = nullptr);
    ~Adb() override;

    // Looks in $ADB, next to the application, ANDROID_SDK_ROOT/ANDROID_HOME platform-tools,
    // the default SDK locations and finally $PATH. Empty when adb cannot be found.
    static QString findExecutable();

    QString executable() const { return m_executable; }
    void setExecutable(const QString &executable) { m_executable = executable; }
    bool isAvailable() const;

    // ---- raw commands -------------------------------------------------------

    // `adb -s <serial> <args>` (no -s when serial is empty).
    static QStringList deviceArgs(const QString &serial, const QStringList &args);

    CommandResult run(const QStringList &args, int timeoutMs = kDefaultTimeoutMs) const;
    // Runs adb without blocking; `callback` is invoked from the event loop (in this
    // object's thread) unless the Adb object is destroyed first.
    void runAsync(const QStringList &args, Callback callback, int timeoutMs = kDefaultTimeoutMs);

    // adb version string such as "34.0.4-debian"; empty if adb cannot be run.
    QString version() const;

    // ---- devices ------------------------------------------------------------

    QList<AdbDevice> devices(CommandResult *result = nullptr) const;
    static QList<AdbDevice> parseDeviceList(const QString &adbDevicesOutput);

    // Result of the most recent refreshDevices() / tracking update.
    QList<AdbDevice> lastKnownDevices() const { return m_devices; }
    // Asynchronously re-reads the device list; emits devicesChanged() if it differs.
    void refreshDevices();
    // Watches `adb track-devices` and keeps the list up to date (emits devicesChanged()).
    // The adb server is started first, on its own: two adb clients that both find no server
    // race to start it and one of them fails. The tracker is restarted automatically if the
    // adb server goes away.
    void startTracking();
    void stopTracking();
    bool isTracking() const { return m_tracking; }

signals:
    void devicesChanged(const QList<QtScrcpy::AdbDevice> &devices);
    // Problems found by background work (refresh failed, adb missing while tracking, ...).
    void errorOccurred(const QString &message);

private:
    void spawnTracker();
    void restartServerOnce();
    void onTrackerStopped(QProcess *tracker);

    QString m_executable;
    QList<AdbDevice> m_devices;

    bool m_refreshing = false;
    int m_failures = 0;
    int m_serverRescues = 0; // how often the hung server was kicked; capped, so this cannot loop
    bool m_refreshQueued = false;

    bool m_tracking = false;
    quint64 m_trackingGeneration = 0; // tells a stale start-server callback from the current one
    QProcess *m_tracker = nullptr;
    QTimer m_debounceTimer;
    QTimer m_restartTimer;
    QTimer m_refreshRetryTimer; // retries a failed device-list read on its own
};

} // namespace QtScrcpy
