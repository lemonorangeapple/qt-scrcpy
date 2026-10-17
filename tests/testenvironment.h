#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <cstdlib>

// Test helpers that keep the lookups of Adb::findExecutable() / Scrcpy::findExecutable() away from
// whatever this machine happens to have installed, and put the environment back afterwards.

// Sets (or unsets, with a null value) an environment variable until destruction.
class ScopedEnvironmentVariable
{
public:
    ScopedEnvironmentVariable(const char *name, const QByteArray &value)
        : m_name(name), m_old(qgetenv(name))
    {
        if (value.isNull())
            qunsetenv(name);
        else
            qputenv(name, value);
    }
    ~ScopedEnvironmentVariable()
    {
        if (m_old.isNull())
            qunsetenv(m_name);
        else
            qputenv(m_name, m_old);
    }

private:
    const char *m_name;
    QByteArray m_old;
};

// $PATH, $HOME (~/Android/Sdk/platform-tools is searched before $PATH), $ADB, $SCRCPY_EXECUTABLE and the
// Android SDK variables, all replaced for the lifetime of the object.
class IsolatedProgramEnvironment
{
public:
    IsolatedProgramEnvironment(const QString &path, const QString &home)
        : m_path("PATH", path.toUtf8()), m_home("HOME", home.toUtf8()),
          m_adb("ADB", QByteArray()), m_scrcpy("SCRCPY_EXECUTABLE", QByteArray()),
          m_sdkRoot("ANDROID_SDK_ROOT", QByteArray()), m_androidHome("ANDROID_HOME", QByteArray()),
          m_localAppData("LOCALAPPDATA", QByteArray())
    {
    }

private:
    ScopedEnvironmentVariable m_path, m_home, m_adb, m_scrcpy, m_sdkRoot, m_androidHome, m_localAppData;
};

// Creates an (empty) executable file; true when the file system lets it be executable at all
// (a TMPDIR on a noexec mount does not).
inline bool makeExecutableFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write("#!/bin/sh\nexit 0\n");
    file.close();
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    return QFileInfo(path).isExecutable();
}
