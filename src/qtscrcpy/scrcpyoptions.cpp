#include "scrcpyoptions.h"

#include <QCoreApplication>
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

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QtScrcpy::ScrcpyOptions", text);
}

QString facingName(CameraFacing facing)
{
    switch (facing) {
    case CameraFacing::Front:
        return QStringLiteral("front");
    case CameraFacing::Back:
        return QStringLiteral("back");
    case CameraFacing::External:
        return QStringLiteral("external");
    case CameraFacing::Any:
        break;
    }
    return {};
}

} // namespace

ScrcpyOptions ScrcpyOptions::forScreen(const QString &serial)
{
    ScrcpyOptions options;
    options.serial = serial;
    return options;
}

ScrcpyOptions ScrcpyOptions::forCamera(const QString &serial, CameraFacing facing)
{
    ScrcpyOptions options;
    options.serial = serial;
    options.videoSource = VideoSource::Camera;
    options.cameraFacing = facing;
    return options;
}

QString ScrcpyOptions::validate(const ScrcpyVersion &version) const
{
    if (videoSource != VideoSource::Camera)
        return {};

    if (version.isValid() && version < cameraMinimumScrcpy()) {
        return tr("Camera mirroring needs scrcpy %1 or newer (found %2)")
            .arg(cameraMinimumScrcpy().toString(), version.toString());
    }
    if (!cameraId.isEmpty() && cameraFacing != CameraFacing::Any)
        return tr("A camera id cannot be combined with a camera facing");
    if (cameraSize && (maxSize > 0 || !cameraAspectRatio.isEmpty()))
        return tr("An explicit camera size cannot be combined with a maximum size or an aspect ratio");
    return {};
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

    const bool camera = videoSource == VideoSource::Camera;

    if (!serial.isEmpty())
        addValue("serial", serial);
    if (camera)
        args << QStringLiteral("--video-source=camera");

    if (maxSize > 0)
        addValue("max-size", QString::number(maxSize));
    if (bitRate > 0) // -b is --bit-rate before scrcpy 2.0 and --video-bit-rate after; --bit-rate is gone in 3.x
        args << QStringLiteral("-b") << QString::number(bitRate);
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

    if (camera) {
        if (!cameraId.isEmpty())
            addValue("camera-id", cameraId);
        else if (cameraFacing != CameraFacing::Any)
            addValue("camera-facing", facingName(cameraFacing));
        if (cameraSize)
            addValue("camera-size", QStringLiteral("%1x%2").arg(cameraSize->width()).arg(cameraSize->height()));
        if (!cameraAspectRatio.isEmpty())
            addValue("camera-ar", cameraAspectRatio);
        if (cameraFps > 0)
            addValue("camera-fps", QString::number(cameraFps));
        addFlag(cameraHighSpeed, "camera-high-speed");
    }
    // scrcpy refuses these when device control is disabled ("Cannot request to ... if
    // control is disabled"), which is always the case for camera mirroring.
    if (!camera && !noControl) {
        addFlag(showTouches, "show-touches");
        addFlag(stayAwake, "stay-awake");
        addFlag(turnScreenOff, "turn-screen-off");
        addFlag(powerOffOnClose, "power-off-on-close");
    }
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
