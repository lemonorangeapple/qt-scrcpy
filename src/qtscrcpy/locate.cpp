#include "locate.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

namespace QtScrcpy {

QString locateExecutable(const QString &name, const QString &envVar, const QStringList &extraDirs)
{
    if (!envVar.isEmpty()) {
        const QString fromEnv = qEnvironmentVariable(envVar.toUtf8().constData());
        if (!fromEnv.isEmpty()) {
            const QString resolved = QStandardPaths::findExecutable(fromEnv);
            if (!resolved.isEmpty())
                return resolved;
        }
    }

    QStringList dirs;
    if (QCoreApplication::instance())
        dirs << QCoreApplication::applicationDirPath();
    dirs << extraDirs;

    const QString found = QStandardPaths::findExecutable(name, dirs);
    if (!found.isEmpty())
        return found;
    return QStandardPaths::findExecutable(name);
}

} // namespace QtScrcpy

namespace QtScrcpy {

QString quotedForWindowsCommandLine(const QString &path)
{
    if (!path.contains(QLatin1Char(' ')) || (path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"'))))
        return path;
    return QLatin1Char('"') + path + QLatin1Char('"');
}

} // namespace QtScrcpy
