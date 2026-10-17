#include "devicemonitor.h"

#include <QHash>
#include <QTimer>

namespace QtScrcpy {

DeviceMonitor::DeviceMonitor(Adb *adb, QObject *parent)
    : QObject(parent)
    , m_adb(adb)
{
    connect(m_adb, &Adb::devicesChanged, this, &DeviceMonitor::update);
}

void DeviceMonitor::start()
{
    // Adb only reports changes, so devices it already knows about have to be replayed.
    const QList<AdbDevice> known = m_adb->lastKnownDevices();
    if (!known.isEmpty())
        QTimer::singleShot(0, this, [this, known] { update(known); });
    m_adb->startTracking();
}

void DeviceMonitor::stop()
{
    m_adb->stopTracking();
}

AdbDevice DeviceMonitor::device(const QString &serial) const
{
    for (const AdbDevice &device : m_devices) {
        if (device.serial == serial)
            return device;
    }
    return {};
}

QList<DeviceEvent> DeviceMonitor::diff(const QList<AdbDevice> &before, const QList<AdbDevice> &after)
{
    using Type = DeviceEvent::Type;

    QHash<QString, AdbDevice> previous;
    for (const AdbDevice &device : before)
        previous.insert(device.serial, device);
    QHash<QString, bool> stillPresent;

    QList<DeviceEvent> events;
    const auto add = [&events](Type type, const AdbDevice &device,
                               AdbDevice::State previousState = AdbDevice::State::Unknown) {
        events.append({type, device, previousState});
    };

    for (const AdbDevice &device : after) {
        stillPresent.insert(device.serial, true);

        const auto old = previous.constFind(device.serial);
        AdbDevice::State oldState = AdbDevice::State::Unknown;
        if (old == previous.constEnd()) {
            add(Type::Attached, device);
        } else {
            if (old->stateText == device.stateText)
                continue; // only properties (model, transport id, ...) changed
            oldState = old->state;
            add(Type::StateChanged, device, oldState);
        }

        const bool isNew = old == previous.constEnd();
        if (device.state == AdbDevice::State::Unauthorized && (isNew || oldState != device.state))
            add(Type::AuthorizationRequired, device);
        if (device.state == AdbDevice::State::Device && (isNew || oldState != device.state))
            add(Type::Ready, device);
    }

    for (const AdbDevice &device : before) {
        if (!stillPresent.contains(device.serial))
            add(Type::Detached, device);
    }
    return events;
}

void DeviceMonitor::update(const QList<AdbDevice> &devices)
{
    const QList<DeviceEvent> events = diff(m_devices, devices);
    m_devices = devices; // handlers see the new list through devices()

    for (const DeviceEvent &event : events) {
        switch (event.type) {
        case DeviceEvent::Type::Attached:
            emit deviceAttached(event.device);
            break;
        case DeviceEvent::Type::Detached:
            emit deviceDetached(event.device);
            break;
        case DeviceEvent::Type::StateChanged:
            emit deviceStateChanged(event.device, event.previousState);
            break;
        case DeviceEvent::Type::AuthorizationRequired:
            emit authorizationRequired(event.device);
            break;
        case DeviceEvent::Type::Ready:
            emit deviceReady(event.device);
            break;
        }
    }
}

} // namespace QtScrcpy
