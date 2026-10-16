#pragma once

#include "qtscrcpy/qtscrcpy_export.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>

namespace QtScrcpy {

enum class CommandError {
    None,
    FailedToStart, // executable missing or not executable
    TimedOut,      // killed after the timeout elapsed
    Crashed,       // terminated by a signal / crashed
};

// Outcome of running an external command (adb, scrcpy --version, ...).
struct QTSCRCPY_EXPORT CommandResult
{
    CommandError error = CommandError::None;
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
    QString errorMessage; // set when error != None

    // True when the process ran to completion and exited with status 0.
    bool ok() const { return error == CommandError::None && exitCode == 0; }

    // Trimmed standard output decoded as UTF-8.
    QString text() const { return QString::fromUtf8(standardOutput).trimmed(); }

    // Best human-readable description of what went wrong ("" when ok()).
    QString failureReason() const
    {
        if (ok())
            return {};
        if (!errorMessage.isEmpty())
            return errorMessage;
        const QString err = QString::fromUtf8(standardError).trimmed();
        if (!err.isEmpty())
            return err;
        const QString out = text();
        if (!out.isEmpty())
            return out;
        return QStringLiteral("exit code %1").arg(exitCode);
    }
};

} // namespace QtScrcpy

Q_DECLARE_METATYPE(QtScrcpy::CommandResult)
