#pragma once

#include "qtscrcpy/qtscrcpy_export.h"

#include <QString>
#include <QStringList>

namespace QtScrcpy {

// The program `program` names, or "" when there is none: a bare name is searched in $PATH, a value
// with a directory part ("./adb", "tools/adb", "/opt/x/adb") is a path, relative to the current
// directory, and is not searched for in $PATH. (QStandardPaths::findExecutable() alone would look
// "./adb" up inside every $PATH entry.) On Windows the usual executable suffixes are tried.
QTSCRCPY_EXPORT QString resolveExecutable(const QString &program);

// Resolves the location of an executable. Search order:
//   1. the file named by the environment variable `envVar` (if set),
//   2. the directory of the running application (bundled copies),
//   3. `extraDirs`,
//   4. $PATH.
// Returns an empty string when nothing was found.
QString locateExecutable(const QString &name, const QString &envVar, const QStringList &extraDirs = {});

} // namespace QtScrcpy

namespace QtScrcpy {

// Wraps `path` in double quotes when it contains a space and is not quoted yet. scrcpy joins
// the program named by $ADB into a Windows command line without quoting it.
QTSCRCPY_EXPORT QString quotedForWindowsCommandLine(const QString &path);

} // namespace QtScrcpy
