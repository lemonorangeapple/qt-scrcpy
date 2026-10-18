#pragma once

#include "adbdevice.h"
#include "commandresult.h"
#include "qtscrcpy/qtscrcpy_export.h"

#include <QList>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

namespace QtScrcpy {

// Wrapper around `hdc` (OpenHarmony / HarmonyOS NEXT device connector): device discovery and
// screenshots. hdc has no scrcpy counterpart, so a screen is mirrored by taking screenshots
// over and over (see snapshotAsync()).
//
// Devices are reported as AdbDevice so that one list can show adb and hdc devices together;
// only serial and state are filled in.
class QTSCRCPY_EXPORT Hdc : public QObject
{
    Q_OBJECT
public:
    static constexpr int kDefaultTimeoutMs = 15 * 1000;
    // `bm` and `snapshot_display` can be slow on a busy device.
    static constexpr int kSlowCommandTimeoutMs = 60 * 1000;

    explicit Hdc(QObject *parent = nullptr);
    explicit Hdc(const QString &executable, QObject *parent = nullptr);
    ~Hdc() override;

    // Looks in $HDC, the toolchains directory of the SDK named by $HOS_SDK_HOME / $OHOS_BASE_SDK_HOME /
    // $DEVECO_SDK_HOME, next to the application and $PATH. Empty when hdc cannot be found.
    static QString findExecutable();

    QString executable() const { return m_executable; }
    void setExecutable(const QString &executable) { m_executable = executable; }
    bool isAvailable() const;

    // Parses `hdc list targets -v` ("<key>  USB  Connected  localhost  hdc", or "[Empty]").
    static QList<AdbDevice> parseTargets(const QString &output);

    // The UDID printed by `hdc shell bm get -u` (needed to sign an application for this device).
    static QString parseUdid(const QString &output);

    QList<AdbDevice> targets() const { return m_targets; }
    // Re-reads the target list without blocking; emits targetsChanged() if it differs.
    void refreshTargets();
    // hdc cannot push device changes, so the list is re-read every `intervalMs`.
    void startPolling(int intervalMs = 2000);
    void stopPolling();

    // Takes a screenshot of the device and hands the image file (JPEG) to `callback`
    // together with an error text; the image is empty when the error is not.
    void snapshotAsync(const QString &serial, std::function<void(const QByteArray &image, const QString &error)> callback);

    // The UDID of a device (needed to sign an app for it); empty when it cannot be read.
    QString deviceUdid(const QString &serial) const;
    void deviceUdidAsync(const QString &serial, std::function<void(const QString &)> callback);

    // Whether the bundle is installed on the device (also false when hdc cannot tell).
    bool isBundleInstalled(const QString &serial, const QString &bundle) const;
    // `hdc install -r <hap>`; `error` is filled from the device's answer when it refuses.
    void installAsync(const QString &serial, const QString &hapFile,
                      std::function<void(bool ok, const QString &error)> callback);

    // Runs `hdc <args>` without blocking; `callback` runs in the event loop.
    void runAsync(const QStringList &args, std::function<void(const CommandResult &)> callback,
                  int timeoutMs = kDefaultTimeoutMs);

signals:
    void targetsChanged(const QList<QtScrcpy::AdbDevice> &targets);

private:

    QString m_executable;
    QList<AdbDevice> m_targets;
    bool m_refreshing = false;
    QTimer m_pollTimer;
    QTemporaryDir m_tempDir;
};

} // namespace QtScrcpy
