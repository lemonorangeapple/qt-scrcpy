#include "adbdevice.h"

#include <QHash>
#include <QRegularExpression>

namespace QtScrcpy {

bool AdbDevice::isNetwork() const
{
    static const QRegularExpression hostPort(QStringLiteral(R"(^(\[[0-9a-fA-F:]+\]|[^:\s]+):\d+$)"));
    return hostPort.match(serial).hasMatch() || serial.contains(QLatin1String("._adb-tls-"));
}

QString AdbDevice::displayName() const
{
    if (!model.isEmpty()) {
        QString name = model;
        name.replace(QLatin1Char('_'), QLatin1Char(' '));
        return name;
    }
    return serial;
}

AdbDevice::State AdbDevice::stateFromString(const QString &text)
{
    static const QHash<QString, State> states = {
        {QStringLiteral("device"), State::Device},
        {QStringLiteral("offline"), State::Offline},
        {QStringLiteral("unauthorized"), State::Unauthorized},
        {QStringLiteral("authorizing"), State::Authorizing},
        {QStringLiteral("connecting"), State::Connecting},
        {QStringLiteral("recovery"), State::Recovery},
        {QStringLiteral("bootloader"), State::Bootloader},
        {QStringLiteral("sideload"), State::Sideload},
        {QStringLiteral("rescue"), State::Rescue},
        {QStringLiteral("host"), State::Host},
    };
    if (text.startsWith(QLatin1String("no permissions")))
        return State::NoPermissions;
    return states.value(text, State::Unknown);
}

} // namespace QtScrcpy
