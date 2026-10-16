#include "scrcpyoptions.h"

#include <QRegularExpression>

namespace QtScrcpy {

QString ScrcpyVersion::toString() const
{
    if (!isValid())
        return {};
    QString text = QStringLiteral("%1.%2").arg(major).arg(minor);
    if (patch != 0)
        text += QStringLiteral(".%1").arg(patch);
    return text;
}

ScrcpyVersion ScrcpyVersion::parse(const QString &versionOutput)
{
    static const QRegularExpression pattern(QStringLiteral(R"(scrcpy\s+v?(\d+)\.(\d+)(?:\.(\d+))?)"));
    const QRegularExpressionMatch match = pattern.match(versionOutput);

    ScrcpyVersion version;
    if (match.hasMatch()) {
        version.major = match.captured(1).toInt();
        version.minor = match.captured(2).toInt();
        version.patch = match.captured(3).toInt(); // 0 when absent
    }
    return version;
}

QStringList ScrcpyOptions::toArguments(const ScrcpyVersion &version) const
{
    const bool isLegacy = version.isValid() && version < ScrcpyVersion{2, 0, 0};

    QStringList args;
    const auto addValue = [&args](const char *name, const QString &value) {
        args << QStringLiteral("--%1=%2").arg(QLatin1String(name), value);
    };
    const auto addFlag = [&args](bool enabled, const char *name) {
        if (enabled)
            args << QStringLiteral("--%1").arg(QLatin1String(name));
    };

    if (!serial.isEmpty())
        addValue("serial", serial);

    if (maxSize > 0)
        addValue("max-size", QString::number(maxSize));
    if (bitRate > 0)
        addValue("bit-rate", QString::number(bitRate));
    if (maxFps > 0)
        addValue("max-fps", QString::number(maxFps));
    if (!crop.isEmpty())
        addValue("crop", crop);

    if (!windowTitle.isEmpty())
        addValue("window-title", windowTitle);
    if (windowPosition) {
        addValue("window-x", QString::number(windowPosition->x()));
        addValue("window-y", QString::number(windowPosition->y()));
    }
    if (windowSize) {
        addValue("window-width", QString::number(windowSize->width()));
        addValue("window-height", QString::number(windowSize->height()));
    }
    addFlag(fullscreen, "fullscreen");
    addFlag(alwaysOnTop, "always-on-top");
    addFlag(borderless, "window-borderless");

    addFlag(showTouches, "show-touches");
    addFlag(stayAwake, "stay-awake");
    addFlag(turnScreenOff, "turn-screen-off");
    addFlag(powerOffOnClose, "power-off-on-close");
    addFlag(disableScreensaver, "disable-screensaver");
    addFlag(noClipboardAutosync, "no-clipboard-autosync");

    addFlag(noControl, "no-control");
    if (noDisplay) // --no-display became --no-playback in scrcpy 2.x; -N works in both
        args << QStringLiteral("-N");
    if (noAudio && !isLegacy)
        args << QStringLiteral("--no-audio");
    if (!recordFile.isEmpty())
        addValue("record", recordFile);

    args << extraArguments;
    return args;
}

} // namespace QtScrcpy
