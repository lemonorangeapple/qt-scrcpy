#pragma once

#include "qtscrcpy/qtscrcpy_export.h"

#include <QString>
#include <QStringList>

namespace QtScrcpy {

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
