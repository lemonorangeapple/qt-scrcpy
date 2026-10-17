#pragma once

#include "adbdevice.h"
#include "commandresult.h"
#include "qtscrcpy/qtscrcpy_export.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

class QProcess;

namespace QtScrcpy {

// Qt wrapper around the `adb` command line client.
//
// Every method that returns a CommandResult (or data derived from one) blocks
// until adb exits or `timeoutMs` elapses; use runAsync() / refreshDevices() /
// startTracking() from the GUI thread for anything that can take long.
//
// For CommandResult returns, ok() is reliable even for adb subcommands that
// print a failure but still exit with status 0 (connect, pair, install, ...).
class QTSCRCPY_EXPORT Adb : public QObject
{
    Q_OBJECT
public:
    static constexpr int kDefaultTimeoutMs = 15 * 1000;
    static constexpr int kTransferTimeoutMs = 10 * 60 * 1000;

    using Callback = std::function<void(const CommandResult &)>;

    // Android key codes for keyEvent().
    enum class Key {
        Home = 3,
        Back = 4,
        VolumeUp = 24,
        VolumeDown = 25,
        Power = 26,
        Enter = 66,
        Menu = 82,
        AppSwitch = 187,
        Sleep = 223,
        Wakeup = 224,
    };
    enum class PackageFilter { All, ThirdParty, System };

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
    CommandResult runOnDevice(const QString &serial, const QStringList &args,
                              int timeoutMs = kDefaultTimeoutMs) const;
    // Runs adb without blocking; `callback` is invoked from the event loop (in this
    // object's thread) unless the Adb object is destroyed first.
    void runAsync(const QStringList &args, Callback callback, int timeoutMs = kDefaultTimeoutMs);

    // ---- server -------------------------------------------------------------

    // adb version string such as "34.0.4-debian"; empty if adb cannot be run.
    QString version() const;
    CommandResult startServer() const;
    // Also stops device tracking, otherwise the tracker would immediately restart the server.
    CommandResult killServer();

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

    // ---- wireless -----------------------------------------------------------

    // `address` is "host" or "host:port" (port defaults to 5555).
    CommandResult connectDevice(const QString &address) const;
    // Empty address disconnects every network device.
    CommandResult disconnectDevice(const QString &address = {}) const;
    // Android 11+ wireless debugging: `hostPort` is the pairing address shown on the phone.
    CommandResult pair(const QString &hostPort, const QString &pairingCode) const;
    // Restart adbd on the device in TCP/IP mode / back in USB mode.
    CommandResult tcpip(const QString &serial, int port = 5555) const;
    CommandResult usb(const QString &serial) const;
    // WLAN IPv4 address of the device; empty if it has none.
    QString deviceIpAddress(const QString &serial) const;

    // ---- device interaction -------------------------------------------------

    CommandResult shell(const QString &serial, const QString &command,
                        int timeoutMs = kDefaultTimeoutMs) const;
    QString getProp(const QString &serial, const QString &key) const;
    // Android API level (e.g. 34 for Android 14); 0 if it cannot be read.
    int androidSdkVersion(const QString &serial) const;
    QMap<QString, QString> properties(const QString &serial, CommandResult *result = nullptr) const;

    CommandResult push(const QString &serial, const QString &localPath, const QString &remotePath,
                       int timeoutMs = kTransferTimeoutMs) const;
    CommandResult pull(const QString &serial, const QString &remotePath, const QString &localPath,
                       int timeoutMs = kTransferTimeoutMs) const;
    CommandResult install(const QString &serial, const QString &apkPath,
                          const QStringList &options = {QStringLiteral("-r")},
                          int timeoutMs = kTransferTimeoutMs) const;
    CommandResult uninstall(const QString &serial, const QString &package, bool keepData = false) const;
    QStringList packages(const QString &serial, PackageFilter filter = PackageFilter::All,
                         CommandResult *result = nullptr) const;

    // PNG image data of the current screen; empty on failure.
    QByteArray screenshot(const QString &serial, CommandResult *result = nullptr) const;

    CommandResult keyEvent(const QString &serial, Key key) const;
    CommandResult keyEvent(const QString &serial, int keyCode) const;
    CommandResult tap(const QString &serial, int x, int y) const;
    CommandResult swipe(const QString &serial, int x1, int y1, int x2, int y2, int durationMs = 300) const;
    // ASCII only (Android's `input text` limitation).
    CommandResult inputText(const QString &serial, const QString &text) const;

    // ---- non-blocking variants ----------------------------------------------
    // Same commands and same CommandResult as the blocking methods above, delivered
    // to `callback` from the event loop (not at all if the Adb object is destroyed first).

    void connectDeviceAsync(const QString &address, Callback callback);
    void androidSdkVersionAsync(const QString &serial, std::function<void(int)> callback);
    void shellAsync(const QString &serial, const QString &command, Callback callback,
                    int timeoutMs = kDefaultTimeoutMs);
    void installAsync(const QString &serial, const QString &apkPath, Callback callback,
                      const QStringList &options = {QStringLiteral("-r")},
                      int timeoutMs = kTransferTimeoutMs);
    void pushAsync(const QString &serial, const QString &localPath, const QString &remotePath,
                   Callback callback, int timeoutMs = kTransferTimeoutMs);
    void pullAsync(const QString &serial, const QString &remotePath, const QString &localPath,
                   Callback callback, int timeoutMs = kTransferTimeoutMs);
    // The PNG data is in CommandResult::standardOutput.
    void screenshotAsync(const QString &serial, Callback callback);

    // ---- port forwarding ----------------------------------------------------
    // Specs use adb syntax, e.g. "tcp:8000", "localabstract:name", "localfilesystem:/path".

    CommandResult forward(const QString &serial, const QString &local, const QString &remote,
                          bool noRebind = false) const;
    CommandResult removeForward(const QString &serial, const QString &local) const;
    CommandResult removeAllForwards(const QString &serial) const;
    CommandResult reverse(const QString &serial, const QString &remote, const QString &local,
                          bool noRebind = false) const;
    CommandResult removeReverse(const QString &serial, const QString &remote) const;
    CommandResult removeAllReverses(const QString &serial) const;

    // Single-quotes `arg` for the device's /bin/sh.
    static QString shellQuote(const QString &arg);

signals:
    void devicesChanged(const QList<QtScrcpy::AdbDevice> &devices);
    // Problems found by background work (refresh failed, adb missing while tracking, ...).
    void errorOccurred(const QString &message);

private:
    void spawnTracker();
    void onTrackerStopped(QProcess *tracker);

    QString m_executable;
    QList<AdbDevice> m_devices;

    bool m_refreshing = false;
    bool m_refreshQueued = false;

    bool m_tracking = false;
    quint64 m_trackingGeneration = 0; // tells a stale start-server callback from the current one
    QProcess *m_tracker = nullptr;
    QTimer m_debounceTimer;
    QTimer m_restartTimer;
};

} // namespace QtScrcpy
