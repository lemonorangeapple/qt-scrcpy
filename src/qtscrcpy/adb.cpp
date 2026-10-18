#include "adb.h"

#include "commandrunner.h"
#include "locate.h"
#include "processtweaks.h"

#include <QDir>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>

#include <utility>

namespace QtScrcpy {

namespace {

constexpr int kTrackerRestartDelayMs = 2000;
// After this many timeouts in a row the message says what to do about it.
constexpr int kFailuresBeforeAdvice = 3;
constexpr int kTrackerDebounceMs = 100;

} // namespace

int Adb::timeoutFromEnvironment(const char *variable, int fallbackMs)
{
    bool ok = false;
    const int value = qEnvironmentVariable(variable).toInt(&ok);
    return ok && value > 0 ? value : fallbackMs;
}

Adb::Adb(QObject *parent)
    : Adb(findExecutable(), parent)
{
}

Adb::Adb(const QString &executable, QObject *parent)
    : QObject(parent)
    , m_executable(executable.isEmpty() ? QStringLiteral("adb") : executable)
{
    // Qt 5 does not register signal parameter types automatically: QSignalSpy (and queued
    // connections) look them up by the names moc put in the meta-object.
    qRegisterMetaType<AdbDevice>("QtScrcpy::AdbDevice");
    qRegisterMetaType<AdbDevice::State>("QtScrcpy::AdbDevice::State");
    qRegisterMetaType<QList<AdbDevice>>("QList<QtScrcpy::AdbDevice>");

    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(kTrackerDebounceMs);
    connect(&m_debounceTimer, &QTimer::timeout, this, &Adb::refreshDevices);

    // A failed device-list read is retried by itself: without this, a machine where the first read times
    // out would show no devices until the tracker happens to report a change.
    m_refreshRetryTimer.setSingleShot(true);
    connect(&m_refreshRetryTimer, &QTimer::timeout, this, [this] {
        if (m_tracking || !m_devices.isEmpty())
            refreshDevices();
    });

    m_restartTimer.setSingleShot(true);
    m_restartTimer.setInterval(kTrackerRestartDelayMs);
    connect(&m_restartTimer, &QTimer::timeout, this, [this] {
        if (m_tracking && !m_tracker) {
            spawnTracker();
            refreshDevices();
        }
    });
}

Adb::~Adb()
{
    stopTracking();
}

QString Adb::findExecutable()
{
    QStringList sdkDirs;
    for (const char *variable : {"ANDROID_SDK_ROOT", "ANDROID_HOME"}) {
        const QString root = qEnvironmentVariable(variable);
        if (!root.isEmpty())
            sdkDirs << QDir(root).filePath(QStringLiteral("platform-tools"));
    }
    const QString home = QDir::homePath();
    sdkDirs << home + QStringLiteral("/Android/Sdk/platform-tools")                   // Linux
            << home + QStringLiteral("/Library/Android/sdk/platform-tools");          // macOS
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        sdkDirs << localAppData + QStringLiteral("/Android/Sdk/platform-tools");      // Windows

    return locateExecutable(QStringLiteral("adb"), QStringLiteral("ADB"), sdkDirs);
}

bool Adb::isAvailable() const
{
    return !resolveExecutable(m_executable).isEmpty();
}

// ---- raw commands ------------------------------------------------------------

QStringList Adb::deviceArgs(const QString &serial, const QStringList &args)
{
    if (serial.isEmpty())
        return args;
    return QStringList{QStringLiteral("-s"), serial} + args;
}

CommandResult Adb::run(const QStringList &args, int timeoutMs) const
{
    return CommandRunner::runSync(m_executable, args, timeoutMs);
}

void Adb::runAsync(const QStringList &args, Callback callback, int timeoutMs)
{
    auto *runner = new CommandRunner(this);
    connect(runner, &CommandRunner::finished, this,
            [callback = std::move(callback)](const CommandResult &result) {
                if (callback)
                    callback(result);
            });
    runner->start(m_executable, args, timeoutMs);
}

QString Adb::version() const
{
    const CommandResult result = run({QStringLiteral("version")});
    if (!result.ok())
        return {};
    const QString text = result.text();

    static const QRegularExpression packageVersion(QStringLiteral(R"(^Version (\S+))"),
                                                   QRegularExpression::MultilineOption);
    static const QRegularExpression protocolVersion(QStringLiteral(R"(version (\S+))"));
    QRegularExpressionMatch match = packageVersion.match(text);
    if (!match.hasMatch())
        match = protocolVersion.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

// ---- devices ------------------------------------------------------------------

QList<AdbDevice> Adb::parseDeviceList(const QString &output)
{
    static const QRegularExpression whitespace(QStringLiteral(R"(\s+)"));
    static const QRegularExpression keyValue(QStringLiteral(R"(^[A-Za-z_]+:\S*$)"));

    QList<AdbDevice> devices;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        // Skips the header and daemon chatter ("* daemon started successfully").
        if (line.isEmpty() || line.startsWith(QLatin1Char('*')) || line.startsWith(QLatin1String("List of devices")))
            continue;

        const QStringList tokens = line.split(whitespace, Qt::SkipEmptyParts);
        if (tokens.size() < 2)
            continue;

        // tokens: serial, state (may contain spaces), then key:value pairs.
        int firstProperty = tokens.size();
        for (int i = 2; i < tokens.size(); ++i) {
            if (keyValue.match(tokens.at(i)).hasMatch()) {
                firstProperty = i;
                break;
            }
        }

        AdbDevice device;
        device.serial = tokens.at(0);
        device.stateText = tokens.mid(1, firstProperty - 1).join(QLatin1Char(' '));
        device.state = AdbDevice::stateFromString(device.stateText);

        for (int i = firstProperty; i < tokens.size(); ++i) {
            const int colon = tokens.at(i).indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            const QString key = tokens.at(i).left(colon);
            const QString value = tokens.at(i).mid(colon + 1);
            if (key == QLatin1String("usb"))
                device.usbPath = value;
            else if (key == QLatin1String("product"))
                device.product = value;
            else if (key == QLatin1String("model"))
                device.model = value;
            else if (key == QLatin1String("device"))
                device.device = value;
            else if (key == QLatin1String("transport_id"))
                device.transportId = value;
        }
        devices.append(device);
    }
    return devices;
}

QList<AdbDevice> Adb::devices(CommandResult *result) const
{
    const CommandResult r = run({QStringLiteral("devices"), QStringLiteral("-l")},
                                timeoutFromEnvironment("QTSCRCPY_ADB_TIMEOUT_MS", kDeviceListTimeoutMs));
    if (result)
        *result = r;
    return r.ok() ? parseDeviceList(r.text()) : QList<AdbDevice>();
}

void Adb::refreshDevices()
{
    if (m_refreshing) {
        m_refreshQueued = true;
        return;
    }
    m_refreshing = true;
    // A devicesChanged()/errorOccurred() handler may delete this Adb object. Keep the callback
    // from touching members (or its own continuation) after that happened.
    const QPointer<Adb> self(this);
    runAsync({QStringLiteral("devices"), QStringLiteral("-l")}, [this, self](const CommandResult &result) {
        if (!self)
            return;
        m_refreshing = false;
        if (result.error == CommandError::TimedOut) {
            // A cold adb server can take a while: keep asking, quietly, before saying anything.
            ++m_failures;
            // Back off a little each time (2 s, 4 s, ... up to 10 s) so a busy machine is not hammered.
            m_refreshRetryTimer.start(qMin(2 * kTrackerRestartDelayMs * m_failures, 10 * 1000));
            if (m_failures == kFailuresBeforeAdvice) {
                emit errorOccurred(tr("adb 响应超时（已重试 %1 次）。首次启动 adb 服务端、或杀毒软件拦截 adb.exe 时常见；"
                                      "程序会继续重试。也可以在环境变量 QTSCRCPY_ADB_TIMEOUT_MS 里把超时改大（毫秒）。")
                                       .arg(m_failures));
                restartServerOnce();
            }
            return;
        }
        if (result.ok()) {
            m_failures = 0;
            m_refreshRetryTimer.stop();
        }
        if (result.ok()) {
            const QList<AdbDevice> devices = parseDeviceList(result.text());
            if (devices != m_devices) {
                m_devices = devices;
                emit devicesChanged(m_devices);
                if (!self)
                    return;
            }
        } else {
            ++m_failures;
            m_refreshRetryTimer.start(qMin(2 * kTrackerRestartDelayMs * m_failures, 10 * 1000));
            if (m_failures <= kFailuresBeforeAdvice)
                emit errorOccurred(result.failureReason());
            else if (m_failures == kFailuresBeforeAdvice + 1)
                restartServerOnce();
            if (!self)
                return;
        }
        if (m_refreshQueued) {
            m_refreshQueued = false;
            refreshDevices();
        }
    }, timeoutFromEnvironment("QTSCRCPY_ADB_TIMEOUT_MS", kDeviceListTimeoutMs));
}

void Adb::restartServerOnce()
{
    // A server that stops answering (a stale one from another adb, a hung USB stack) is what
    // `adb kill-server` fixes. Do it once: repeating it would fight with the user's other tools.
    constexpr int kMaxRescues = 2;
    if (m_serverRescues >= kMaxRescues) {
        emit errorOccurred(tr("adb 一直不响应，已停止重启它。请手动运行 `adb kill-server`，"
                              "或检查 adb 是否被安全软件拦截、是否与另一个版本的 adb 冲突。"));
        return;
    }
    ++m_serverRescues;
    emit errorOccurred(tr("adb 长时间没有响应，正在重启 adb 服务端（kill-server + start-server）…"));
    const QPointer<Adb> self(this);
    runAsync({QStringLiteral("kill-server")}, [this, self](const CommandResult &) {
        if (!self)
            return;
        const bool wasTracking = m_tracking;
        stopTracking();
        m_failures = 0;
        if (wasTracking)
            startTracking();
        else
            refreshDevices();
    });
}

void Adb::startTracking()
{
    if (m_tracking)
        return;
    // (the rescue counter is deliberately not reset: it caps the kill-server loop over the whole run)
    m_tracking = true;
    const quint64 generation = ++m_trackingGeneration;

    // The tracker and the first `devices -l` would both try to start a server that is not
    // running yet, and with adb 36 one of them then fails ("failed to start daemon"): bring the
    // server up first.
    const QPointer<Adb> self(this);
    runAsync({QStringLiteral("start-server")}, [this, self, generation](const CommandResult &result) {
        if (!self || !m_tracking || generation != m_trackingGeneration)
            return; // stopTracking() (and maybe a new startTracking()) happened meanwhile
        if (result.error == CommandError::FailedToStart) {
            // No adb binary: nothing to retry.
            m_tracking = false;
            emit errorOccurred(result.failureReason());
            return;
        }
        if (!result.ok()) {
            emit errorOccurred(result.failureReason());
            if (!self)
                return;
            m_restartTimer.start(); // try again; the tracker also starts a server by itself
            return;
        }
        spawnTracker();
        refreshDevices();
    }, timeoutFromEnvironment("QTSCRCPY_ADB_TIMEOUT_MS", kServerStartTimeoutMs));
}

void Adb::spawnTracker()
{
    auto *tracker = new QProcess(this);
    m_tracker = tracker;
    detail::suppressConsoleWindow(*tracker);

    // The tracker's output format varies between adb versions, so any output only
    // acts as a "something changed" hint and the list is re-read with `devices -l`.
    connect(tracker, &QProcess::readyReadStandardOutput, this, [this, tracker] {
        tracker->readAllStandardOutput();
        m_debounceTimer.start();
    });
    // QProcess::finished is overloaded in Qt 5: pick the (exitCode, status) version explicitly.
    connect(tracker, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, tracker] { onTrackerStopped(tracker); });
    connect(tracker, &QProcess::errorOccurred, this, [this, tracker](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        // Without an adb binary there is nothing to retry.
        const QString message = tracker->errorString();
        m_tracking = false;
        onTrackerStopped(tracker);
        emit errorOccurred(message);
    });
    tracker->start(m_executable, {QStringLiteral("track-devices")});
}

void Adb::onTrackerStopped(QProcess *tracker)
{
    if (tracker != m_tracker)
        return;
    m_tracker = nullptr;
    tracker->deleteLater();
    if (m_tracking)
        m_restartTimer.start();
}

void Adb::stopTracking()
{
    m_tracking = false;
    ++m_trackingGeneration;
    m_restartTimer.stop();
    m_refreshRetryTimer.stop();
    m_debounceTimer.stop();
    if (QProcess *tracker = m_tracker) {
        m_tracker = nullptr;
        tracker->disconnect(this);
        tracker->kill();
        tracker->waitForFinished(1000);
        delete tracker;
    }
}

} // namespace QtScrcpy
