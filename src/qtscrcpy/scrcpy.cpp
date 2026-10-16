#include "scrcpy.h"

#include "commandrunner.h"
#include "locate.h"

#include <QProcessEnvironment>
#include <QStandardPaths>

namespace QtScrcpy {

Scrcpy::Scrcpy(QObject *parent)
    : QObject(parent)
{
    const QString found = findExecutable();
    m_executable = found.isEmpty() ? QStringLiteral("scrcpy") : found;

    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_process.state() != QProcess::NotRunning)
            m_process.kill();
    });

    connect(&m_process, &QProcess::started, this, [this] {
        setState(State::Running);
        emit started();
    });
    connect(&m_process, &QProcess::finished, this, &Scrcpy::handleFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Crashes are reported through finished(); a failed launch has no finished().
        if (error != QProcess::FailedToStart)
            return;
        setState(State::Stopped);
        emit errorOccurred(m_process.errorString());
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { readOutput(false); });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { readOutput(true); });
}

Scrcpy::~Scrcpy()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.disconnect(this);
        m_process.terminate();
        if (!m_process.waitForFinished(1500)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
}

QString Scrcpy::findExecutable()
{
    return locateExecutable(QStringLiteral("scrcpy"), QStringLiteral("SCRCPY_EXECUTABLE"));
}

void Scrcpy::setExecutable(const QString &executable)
{
    m_executable = executable;
}

bool Scrcpy::isAvailable() const
{
    return !QStandardPaths::findExecutable(m_executable).isEmpty();
}

ScrcpyVersion Scrcpy::version() const
{
    if (m_versionOf != m_executable) {
        const CommandResult result = CommandRunner::runSync(m_executable, {QStringLiteral("--version")}, 5000);
        m_version = result.ok() ? ScrcpyVersion::parse(result.text()) : ScrcpyVersion();
        // Remember failures too, so a missing binary is not re-probed on every call.
        m_versionOf = m_executable;
    }
    return m_version;
}

qint64 Scrcpy::processId() const
{
    return m_process.state() == QProcess::NotRunning ? 0 : m_process.processId();
}

bool Scrcpy::start(const ScrcpyOptions &options)
{
    if (m_state != State::Stopped) {
        emit errorOccurred(tr("scrcpy is already running"));
        return false;
    }
    if (!isAvailable()) {
        emit errorOccurred(tr("scrcpy executable not found: %1").arg(m_executable));
        return false;
    }

    const ScrcpyVersion scrcpyVersion = version();
    const QString problem = options.validate(scrcpyVersion);
    if (!problem.isEmpty()) {
        emit errorOccurred(problem);
        return false;
    }
    m_arguments = options.toArguments(scrcpyVersion);

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (!m_adbExecutable.isEmpty())
        environment.insert(QStringLiteral("ADB"), m_adbExecutable);
    m_process.setProcessEnvironment(environment);

    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_stopRequested = false;
    setState(State::Starting);
    m_process.start(m_executable, m_arguments);
    return true;
}

void Scrcpy::stop(int gracePeriodMs)
{
    if (m_process.state() == QProcess::NotRunning)
        return;
    m_stopRequested = true;
    setState(State::Stopping);
    m_process.terminate();
    m_killTimer.start(gracePeriodMs);
}

void Scrcpy::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void Scrcpy::readOutput(bool isError)
{
    QByteArray &buffer = isError ? m_stderrBuffer : m_stdoutBuffer;
    buffer += isError ? m_process.readAllStandardError() : m_process.readAllStandardOutput();

    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(buffer.left(newline)).remove(QLatin1Char('\r'));
        buffer.remove(0, newline + 1);
        emit outputReceived(line, isError);
    }
}

void Scrcpy::flushOutput(bool isError)
{
    readOutput(isError);
    QByteArray &buffer = isError ? m_stderrBuffer : m_stdoutBuffer;
    if (!buffer.isEmpty()) {
        emit outputReceived(QString::fromUtf8(buffer).remove(QLatin1Char('\r')), isError);
        buffer.clear();
    }
}

void Scrcpy::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    m_killTimer.stop();
    flushOutput(false);
    flushOutput(true);
    setState(State::Stopped);
    // A process we terminated ourselves "crashes" with SIGTERM/SIGKILL on Unix; that's
    // the expected way to stop it, not a crash worth reporting.
    emit finished(exitCode, status == QProcess::CrashExit && !m_stopRequested);
}

} // namespace QtScrcpy
