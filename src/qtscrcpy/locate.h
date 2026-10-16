#pragma once

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
