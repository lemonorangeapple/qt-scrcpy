#pragma once

#include "qtscrcpy/qtscrcpy_export.h"

#include <QList>
#include <QMetaType>
#include <QString>

namespace QtScrcpy {

// One line of `adb devices -l`.
struct QTSCRCPY_EXPORT AdbDevice
{
    enum class State {
        Unknown,
        Device,        // online and usable
        Offline,
        Unauthorized,  // waiting for the "Allow USB debugging" prompt
        NoPermissions, // Linux udev problem
        Authorizing,
        Connecting,
        Recovery,
        Bootloader,
        Sideload,
        Rescue,
        Host,
    };

    QString serial;
    State state = State::Unknown;
    QString stateText; // raw state string as printed by adb
    QString usbPath;   // e.g. "1-1.2"
    QString product;
    QString model;
    QString device;
    QString transportId;

    bool isOnline() const { return state == State::Device; }
    bool isEmulator() const { return serial.startsWith(QLatin1String("emulator-")); }
    // Wireless device, i.e. serial of the form "host:port" (also mDNS "adb-xxx._adb-tls-connect._tcp").
    bool isNetwork() const;

    // Model name with underscores replaced by spaces, falling back to the serial.
    QString displayName() const;

    static State stateFromString(const QString &text);

    friend bool operator==(const AdbDevice &a, const AdbDevice &b)
    {
        return a.serial == b.serial && a.state == b.state && a.stateText == b.stateText
               && a.usbPath == b.usbPath && a.product == b.product && a.model == b.model
               && a.device == b.device && a.transportId == b.transportId;
    }
    friend bool operator!=(const AdbDevice &a, const AdbDevice &b) { return !(a == b); }
};

} // namespace QtScrcpy

Q_DECLARE_METATYPE(QtScrcpy::AdbDevice)
Q_DECLARE_METATYPE(QList<QtScrcpy::AdbDevice>)
