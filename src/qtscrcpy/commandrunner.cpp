#include "commandrunner.h"

namespace QtScrcpy {

CommandResult CommandRunner::runSync(const QString &program,
                                     const QStringList &arguments,
                                     int timeoutMs,
                                     const QProcessEnvironment &environment)
{
    CommandResult result;

    QProcess process;
    if (!environment.isEmpty())
        process.setProcessEnvironment(environment);
    process.start(program, arguments);

    if (!process.waitForStarted(timeoutMs)) {
        result.error = CommandError::FailedToStart;
        result.errorMessage = process.errorString();
        return result;
    }

    const bool finished = process.waitForFinished(timeoutMs);
    if (!finished && process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
        result.error = CommandError::TimedOut;
        result.errorMessage = QStringLiteral("'%1' timed out after %2 ms").arg(program).arg(timeoutMs);
    } else if (process.exitStatus() == QProcess::CrashExit) {
        result.error = CommandError::Crashed;
        result.errorMessage = process.errorString();
    }

    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    if (result.error == CommandError::None)
        result.exitCode = process.exitCode();
    return result;
}

CommandRunner::CommandRunner(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_result.standardOutput += m_process.readAllStandardOutput();
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_result.standardError += m_process.readAllStandardError();
    });
    connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        m_result.exitCode = exitCode;
        if (status == QProcess::CrashExit)
            finish(CommandError::Crashed, m_process.errorString());
        else
            finish(CommandError::None);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Crashed is reported again through finished(); only FailedToStart has no
        // finished() companion.
        if (error == QProcess::FailedToStart)
            finish(CommandError::FailedToStart, m_process.errorString());
    });
    connect(&m_timer, &QTimer::timeout, this, [this] {
        finish(CommandError::TimedOut, QStringLiteral("command timed out"));
    });
}

CommandRunner::~CommandRunner()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.disconnect(this);
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void CommandRunner::start(const QString &program,
                          const QStringList &arguments,
                          int timeoutMs,
                          const QProcessEnvironment &environment)
{
    if (!environment.isEmpty())
        m_process.setProcessEnvironment(environment);
    if (timeoutMs >= 0)
        m_timer.start(timeoutMs);
    m_process.start(program, arguments);
}

void CommandRunner::finish(CommandError error, const QString &message)
{
    if (m_done)
        return;
    m_done = true;

    m_timer.stop();
    if (m_process.state() != QProcess::NotRunning) {
        // Timed-out: stop the child and drop whatever it still reports.
        m_process.disconnect(this);
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    m_result.standardOutput += m_process.readAllStandardOutput();
    m_result.standardError += m_process.readAllStandardError();

    m_result.error = error;
    m_result.errorMessage = message;
    if (error != CommandError::None && error != CommandError::Crashed)
        m_result.exitCode = -1;

    emit finished(m_result);
    deleteLater();
}

} // namespace QtScrcpy
