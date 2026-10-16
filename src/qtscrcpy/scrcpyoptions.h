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

// The scrcpy command line options exposed by this wrapper. Defaults mean "scrcpy's default".
struct QTSCRCPY_EXPORT ScrcpyOptions
{
    QString serial;      // --serial; may be empty when exactly one device is connected

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

    // device behaviour
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

    // Builds the argument list for a given scrcpy version. Options whose spelling
    // changed between releases are mapped; options the version lacks are skipped.
    // An invalid (unknown) version is treated as a current release.
    QStringList toArguments(const ScrcpyVersion &version = {}) const;
};

} // namespace QtScrcpy
