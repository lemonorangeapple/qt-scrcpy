#pragma once

#include "adb.h"
#include "adbdevice.h"
#include "qtscrcpy/qtscrcpy_export.h"

#include <QList>
#include <QObject>

namespace QtScrcpy {

// One thing that happened to a device between two device lists.
struct QTSCRCPY_EXPORT DeviceEvent
{
    enum class Type {
        Attached,              // a serial appeared (in any state)
        Detached,              // a serial disappeared; `device` is its last known state
        StateChanged,          // known serial, new adb state; see previousState
        AuthorizationRequired, // entered "unauthorized": the phone is showing its USB debugging prompt
        Ready,                 // entered "device": usable now, either freshly plugged in or just allowed
    };

    Type type = Type::Attached;
    AdbDevice device;
    AdbDevice::State previousState = AdbDevice::State::Unknown; // StateChanged only
};

// Turns the stream of device lists from Adb into plug/unplug/authorization events,
// i.e. the "plug in the cable -> phone asks for authorization -> user taps Allow ->
// device becomes usable" sequence:
//
//   deviceAttached -> authorizationRequired -> deviceStateChanged -> deviceReady
//
// A device that is already authorized goes straight from deviceAttached to deviceReady.
// The monitor does not own the Adb object.
class QTSCRCPY_EXPORT DeviceMonitor : public QObject
{
    Q_OBJECT
public:
    explicit DeviceMonitor(Adb *adb, QObject *parent = nullptr);

    // Starts adb device tracking. Devices that are already connected are reported as if
    // they had just been plugged in.
    void start();
    void stop();

    // Devices as of the last event.
    QList<AdbDevice> devices() const { return m_devices; }
    AdbDevice device(const QString &serial) const;

    // The events that turn list `before` into list `after`, in the order they are emitted.
    static QList<DeviceEvent> diff(const QList<AdbDevice> &before, const QList<AdbDevice> &after);

signals:
    void deviceAttached(const QtScrcpy::AdbDevice &device);
    void deviceDetached(const QtScrcpy::AdbDevice &device);
    void deviceStateChanged(const QtScrcpy::AdbDevice &device, QtScrcpy::AdbDevice::State previous);
    void authorizationRequired(const QtScrcpy::AdbDevice &device);
    void deviceReady(const QtScrcpy::AdbDevice &device);

private:
    void update(const QList<AdbDevice> &devices);

    Adb *m_adb;
    QList<AdbDevice> m_devices;
};

} // namespace QtScrcpy
