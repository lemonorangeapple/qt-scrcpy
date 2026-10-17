#pragma once

#include "qtscrcpy/qtscrcpy_export.h"

#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>
#include <tuple>

namespace QtScrcpy {

// scrcpy release number, e.g. 1.25 or 2.4.1.
struct QTSCRCPY_EXPORT ScrcpyVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;

    bool isValid() const { return major > 0; }
    QString toString() const;

    // Extracts the version from `scrcpy --version` output ("scrcpy 2.4 <https://...>").
    static ScrcpyVersion parse(const QString &versionOutput);

    friend bool operator==(const ScrcpyVersion &a, const ScrcpyVersion &b)
    {
        return std::tie(a.major, a.minor, a.patch) == std::tie(b.major, b.minor, b.patch);
    }
    friend bool operator!=(const ScrcpyVersion &a, const ScrcpyVersion &b) { return !(a == b); }
    friend bool operator<(const ScrcpyVersion &a, const ScrcpyVersion &b)
    {
        return std::tie(a.major, a.minor, a.patch) < std::tie(b.major, b.minor, b.patch);
    }
    friend bool operator>=(const ScrcpyVersion &a, const ScrcpyVersion &b) { return !(a < b); }
};

// What scrcpy captures.
enum class VideoSource {
    Display, // the device screen (default)
    Camera,  // a device camera; needs scrcpy >= 2.2 and Android 12+
};

enum class CameraFacing { Any, Front, Back, External };

// The scrcpy command line options exposed by this wrapper. Defaults mean "scrcpy's default".
struct QTSCRCPY_EXPORT ScrcpyOptions
{
    // Camera mirroring needs at least this scrcpy release and this Android API level (12).
    static ScrcpyVersion cameraMinimumScrcpy() { return {2, 2, 0}; }
    static constexpr int kCameraMinimumSdk = 31;

    // Ready-made option sets for the two modes a user picks between.
    static ScrcpyOptions forScreen(const QString &serial);
    static ScrcpyOptions forCamera(const QString &serial, CameraFacing facing = CameraFacing::Back);

    QString serial;      // --serial; may be empty when exactly one device is connected

    VideoSource videoSource = VideoSource::Display;

    // video
    int maxSize = 0;     // --max-size: limit width/height (0 = device resolution)
    int bitRate = 0;     // --bit-rate in bits per second (0 = scrcpy default, 8 Mbit/s)
    int maxFps = 0;      // --max-fps (0 = unlimited)
    QString crop;        // --crop "width:height:x:y"

    // window
    QString windowTitle;
    std::optional<QPoint> windowPosition;
    std::optional<QSize> windowSize;
    bool fullscreen = false;
    bool alwaysOnTop = false;
    bool borderless = false;

    // camera (VideoSource::Camera only; ignored otherwise)
    CameraFacing cameraFacing = CameraFacing::Any; // first camera facing that way; excludes cameraId
    QString cameraId;                              // an id as listed by `scrcpy --list-cameras`
    std::optional<QSize> cameraSize;               // excludes maxSize and cameraAspectRatio
    QString cameraAspectRatio;                     // "4:3", "1.6" or "sensor"
    int cameraFps = 0;                             // 0 = Android default (30)
    bool cameraHighSpeed = false;

    // device behaviour. These need device control, which is off for camera mirroring and
    // with noControl; scrcpy would refuse to start, so they are not passed on then.
    bool showTouches = false;       // --show-touches
    bool stayAwake = false;         // --stay-awake
    bool turnScreenOff = false;     // --turn-screen-off
    bool powerOffOnClose = false;   // --power-off-on-close
    bool disableScreensaver = false;
    bool noClipboardAutosync = false;

    // modes
    bool noControl = false;         // mirror only, no input
    bool noDisplay = false;         // no window; scrcpy 1.x refuses it unless recordFile is also set
    bool noAudio = false;           // scrcpy >= 2.0 only; older versions never forward audio
    QString recordFile;             // --record (format follows the extension)

    // Passed through verbatim for options this wrapper does not model.
    QStringList extraArguments;

    // Empty if these options can be used with `version`, otherwise why not (camera mode
    // with a scrcpy that lacks it, or combinations scrcpy itself rejects). An invalid
    // (unknown) version is treated as a current release. Scrcpy::start() calls this.
    QString validate(const ScrcpyVersion &version = {}) const;

    // Builds the argument list for a given scrcpy version. Options whose spelling
    // changed between releases are mapped; options the version lacks are skipped.
    // An invalid (unknown) version is treated as a current release.
    QStringList toArguments(const ScrcpyVersion &version = {}) const;
};

} // namespace QtScrcpy
