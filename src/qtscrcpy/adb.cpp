#include "adb.h"

#include "commandrunner.h"
#include "locate.h"

#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace QtScrcpy {

namespace {

constexpr int kTrackerRestartDelayMs = 2000;
constexpr int kTrackerDebounceMs = 100;

// Some adb subcommands report failure only in their output. Turn that into a failed result.
CommandResult requireOutput(CommandResult result, const QStringList &successMarkers)
{
    if (!result.ok())
        return result;
    const QString output = result.text();
    for (const QString &marker : successMarkers) {
        if (output.contains(marker, Qt::CaseInsensitive))
            return result;
    }
    result.exitCode = 1;
    result.errorMessage = output.isEmpty() ? QString::fromUtf8(result.standardError).trimmed() : output;
    return result;
}

CommandResult rejectOnOutput(CommandResult result, const QStringList &failureMarkers)
{
    if (!result.ok())
        return result;
    const QString output = result.text();
    for (const QString &marker : failureMarkers) {
        if (output.contains(marker)) {
            result.exitCode = 1;
            result.errorMessage = output;
            break;
        }
    }
    return result;
}

QStringList connectArgs(const QString &address)
{
    return {QStringLiteral("connect"), address};
}

QStringList installArgs(const QString &apkPath, const QStringList &options)
{
    return QStringList{QStringLiteral("install")} + options + QStringList{apkPath};
}

QStringList screenshotArgs()
{
    return {QStringLiteral("exec-out"), QStringLiteral("screencap"), QStringLiteral("-p")};
}

CommandResult checkConnect(CommandResult result)
{
    return requireOutput(std::move(result), {QStringLiteral("connected to")}); // also "already connected to"
}

CommandResult checkInstall(CommandResult result)
{
    return rejectOnOutput(std::move(result), {QStringLiteral("Failure")});
}

constexpr int kConnectTimeoutMs = 10 * 1000;
constexpr int kScreenshotTimeoutMs = 30 * 1000;

} // namespace

Adb::Adb(QObject *parent)
    : Adb(findExecutable(), parent)
{
}

Adb::Adb(const QString &executable, QObject *parent)
    : QObject(parent)
    , m_executable(executable.isEmpty() ? QStringLiteral("adb") : executable)
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(kTrackerDebounceMs);
    connect(&m_debounceTimer, &QTimer::timeout, this, &Adb::refreshDevices);

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
    return !QStandardPaths::findExecutable(m_executable).isEmpty();
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

CommandResult Adb::runOnDevice(const QString &serial, const QStringList &args, int timeoutMs) const
{
    return run(deviceArgs(serial, args), timeoutMs);
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

// ---- server -------------------------------------------------------------------

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

CommandResult Adb::startServer() const
{
    return run({QStringLiteral("start-server")}, 30 * 1000);
}

CommandResult Adb::killServer()
{
    stopTracking();
    return run({QStringLiteral("kill-server")});
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
    const CommandResult r = run({QStringLiteral("devices"), QStringLiteral("-l")});
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
    runAsync({QStringLiteral("devices"), QStringLiteral("-l")}, [this](const CommandResult &result) {
        m_refreshing = false;
        if (result.ok()) {
            const QList<AdbDevice> devices = parseDeviceList(result.text());
            if (devices != m_devices) {
                m_devices = devices;
                emit devicesChanged(m_devices);
            }
        } else {
            emit errorOccurred(result.failureReason());
        }
        if (m_refreshQueued) {
            m_refreshQueued = false;
            refreshDevices();
        }
    });
}

void Adb::startTracking()
{
    if (m_tracking)
        return;
    m_tracking = true;
    spawnTracker();
    refreshDevices();
}

void Adb::spawnTracker()
{
    auto *tracker = new QProcess(this);
    m_tracker = tracker;

    // The tracker's output format varies between adb versions, so any output only
    // acts as a "something changed" hint and the list is re-read with `devices -l`.
    connect(tracker, &QProcess::readyReadStandardOutput, this, [this, tracker] {
        tracker->readAllStandardOutput();
        m_debounceTimer.start();
    });
    connect(tracker, &QProcess::finished, this, [this, tracker] { onTrackerStopped(tracker); });
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
    m_restartTimer.stop();
    m_debounceTimer.stop();
    if (QProcess *tracker = m_tracker) {
        m_tracker = nullptr;
        tracker->disconnect(this);
        tracker->kill();
        tracker->waitForFinished(1000);
        delete tracker;
    }
}

// ---- wireless -----------------------------------------------------------------

CommandResult Adb::connectDevice(const QString &address) const
{
    return checkConnect(run(connectArgs(address), kConnectTimeoutMs));
}

CommandResult Adb::disconnectDevice(const QString &address) const
{
    QStringList args{QStringLiteral("disconnect")};
    if (!address.isEmpty())
        args << address;
    return run(args);
}

CommandResult Adb::pair(const QString &hostPort, const QString &pairingCode) const
{
    return requireOutput(run({QStringLiteral("pair"), hostPort, pairingCode}, 20 * 1000),
                         {QStringLiteral("Successfully paired")});
}

CommandResult Adb::tcpip(const QString &serial, int port) const
{
    return runOnDevice(serial, {QStringLiteral("tcpip"), QString::number(port)});
}

CommandResult Adb::usb(const QString &serial) const
{
    return runOnDevice(serial, {QStringLiteral("usb")});
}

QString Adb::deviceIpAddress(const QString &serial) const
{
    static const QRegularExpression addrInet(QStringLiteral(R"(inet (\d+\.\d+\.\d+\.\d+))"));
    static const QRegularExpression routeSrc(QStringLiteral(R"(wlan\d+.*\bsrc (\d+\.\d+\.\d+\.\d+))"));

    CommandResult result = shell(serial, QStringLiteral("ip -f inet addr show wlan0"));
    if (result.ok()) {
        const QRegularExpressionMatch match = addrInet.match(result.text());
        if (match.hasMatch())
            return match.captured(1);
    }

    result = shell(serial, QStringLiteral("ip route"));
    if (result.ok()) {
        const QRegularExpressionMatch match = routeSrc.match(result.text());
        if (match.hasMatch())
            return match.captured(1);
    }
    return {};
}

// ---- device interaction -------------------------------------------------------

CommandResult Adb::shell(const QString &serial, const QString &command, int timeoutMs) const
{
    return runOnDevice(serial, {QStringLiteral("shell"), command}, timeoutMs);
}

QString Adb::getProp(const QString &serial, const QString &key) const
{
    const CommandResult result = runOnDevice(serial, {QStringLiteral("shell"), QStringLiteral("getprop"), key});
    return result.ok() ? result.text() : QString();
}

int Adb::androidSdkVersion(const QString &serial) const
{
    return getProp(serial, QStringLiteral("ro.build.version.sdk")).toInt();
}

QMap<QString, QString> Adb::properties(const QString &serial, CommandResult *result) const
{
    static const QRegularExpression line(QStringLiteral(R"(^\[(.*?)\]: \[(.*)\]$)"),
                                         QRegularExpression::MultilineOption);

    const CommandResult r = runOnDevice(serial, {QStringLiteral("shell"), QStringLiteral("getprop")});
    if (result)
        *result = r;

    QMap<QString, QString> props;
    if (!r.ok())
        return props;
    const QString text = QString::fromUtf8(r.standardOutput).remove(QLatin1Char('\r'));
    auto it = line.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        props.insert(match.captured(1), match.captured(2));
    }
    return props;
}

CommandResult Adb::push(const QString &serial, const QString &localPath, const QString &remotePath,
                        int timeoutMs) const
{
    return runOnDevice(serial, {QStringLiteral("push"), localPath, remotePath}, timeoutMs);
}

CommandResult Adb::pull(const QString &serial, const QString &remotePath, const QString &localPath,
                        int timeoutMs) const
{
    return runOnDevice(serial, {QStringLiteral("pull"), remotePath, localPath}, timeoutMs);
}

CommandResult Adb::install(const QString &serial, const QString &apkPath, const QStringList &options,
                           int timeoutMs) const
{
    return checkInstall(runOnDevice(serial, installArgs(apkPath, options), timeoutMs));
}

CommandResult Adb::uninstall(const QString &serial, const QString &package, bool keepData) const
{
    QStringList args{QStringLiteral("uninstall")};
    if (keepData)
        args << QStringLiteral("-k");
    args << package;
    return rejectOnOutput(runOnDevice(serial, args, 60 * 1000), {QStringLiteral("Failure")});
}

QStringList Adb::packages(const QString &serial, PackageFilter filter, CommandResult *result) const
{
    QString command = QStringLiteral("pm list packages");
    if (filter == PackageFilter::ThirdParty)
        command += QStringLiteral(" -3");
    else if (filter == PackageFilter::System)
        command += QStringLiteral(" -s");

    const CommandResult r = shell(serial, command, 30 * 1000);
    if (result)
        *result = r;

    QStringList packages;
    if (!r.ok())
        return packages;
    const QStringList lines = r.text().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        const QLatin1String prefix("package:");
        if (trimmed.startsWith(prefix))
            packages << trimmed.mid(prefix.size());
    }
    packages.sort();
    return packages;
}

QByteArray Adb::screenshot(const QString &serial, CommandResult *result) const
{
    // exec-out (unlike `shell`) leaves the binary stream untouched.
    const CommandResult r = runOnDevice(serial, screenshotArgs(), kScreenshotTimeoutMs);
    if (result)
        *result = r;
    return r.ok() ? r.standardOutput : QByteArray();
}

CommandResult Adb::keyEvent(const QString &serial, Key key) const
{
    return keyEvent(serial, static_cast<int>(key));
}

CommandResult Adb::keyEvent(const QString &serial, int keyCode) const
{
    return shell(serial, QStringLiteral("input keyevent %1").arg(keyCode));
}

CommandResult Adb::tap(const QString &serial, int x, int y) const
{
    return shell(serial, QStringLiteral("input tap %1 %2").arg(x).arg(y));
}

CommandResult Adb::swipe(const QString &serial, int x1, int y1, int x2, int y2, int durationMs) const
{
    return shell(serial, QStringLiteral("input swipe %1 %2 %3 %4 %5").arg(x1).arg(y1).arg(x2).arg(y2).arg(durationMs));
}

CommandResult Adb::inputText(const QString &serial, const QString &text) const
{
    QString escaped = text;
    escaped.replace(QLatin1Char(' '), QLatin1String("%s")); // `input text` encodes spaces as %s
    return shell(serial, QStringLiteral("input text ") + shellQuote(escaped));
}

// ---- non-blocking variants ----------------------------------------------------

void Adb::connectDeviceAsync(const QString &address, Callback callback)
{
    runAsync(connectArgs(address), [callback = std::move(callback)](const CommandResult &result) {
        if (callback)
            callback(checkConnect(result));
    }, kConnectTimeoutMs);
}

void Adb::androidSdkVersionAsync(const QString &serial, std::function<void(int)> callback)
{
    runAsync(deviceArgs(serial, {QStringLiteral("shell"), QStringLiteral("getprop"),
                                 QStringLiteral("ro.build.version.sdk")}),
             [callback = std::move(callback)](const CommandResult &result) {
                 if (callback)
                     callback(result.ok() ? result.text().toInt() : 0);
             });
}

void Adb::shellAsync(const QString &serial, const QString &command, Callback callback, int timeoutMs)
{
    runAsync(deviceArgs(serial, {QStringLiteral("shell"), command}), std::move(callback), timeoutMs);
}

void Adb::installAsync(const QString &serial, const QString &apkPath, Callback callback,
                       const QStringList &options, int timeoutMs)
{
    runAsync(deviceArgs(serial, installArgs(apkPath, options)),
             [callback = std::move(callback)](const CommandResult &result) {
                 if (callback)
                     callback(checkInstall(result));
             }, timeoutMs);
}

void Adb::pushAsync(const QString &serial, const QString &localPath, const QString &remotePath,
                    Callback callback, int timeoutMs)
{
    runAsync(deviceArgs(serial, {QStringLiteral("push"), localPath, remotePath}), std::move(callback), timeoutMs);
}

void Adb::pullAsync(const QString &serial, const QString &remotePath, const QString &localPath,
                    Callback callback, int timeoutMs)
{
    runAsync(deviceArgs(serial, {QStringLiteral("pull"), remotePath, localPath}), std::move(callback), timeoutMs);
}

void Adb::screenshotAsync(const QString &serial, Callback callback)
{
    runAsync(deviceArgs(serial, screenshotArgs()), std::move(callback), kScreenshotTimeoutMs);
}

// ---- port forwarding ----------------------------------------------------------

CommandResult Adb::forward(const QString &serial, const QString &local, const QString &remote,
                           bool noRebind) const
{
    QStringList args{QStringLiteral("forward")};
    if (noRebind)
        args << QStringLiteral("--no-rebind");
    args << local << remote;
    return runOnDevice(serial, args);
}

CommandResult Adb::removeForward(const QString &serial, const QString &local) const
{
    return runOnDevice(serial, {QStringLiteral("forward"), QStringLiteral("--remove"), local});
}

CommandResult Adb::removeAllForwards(const QString &serial) const
{
    return runOnDevice(serial, {QStringLiteral("forward"), QStringLiteral("--remove-all")});
}

CommandResult Adb::reverse(const QString &serial, const QString &remote, const QString &local,
                           bool noRebind) const
{
    QStringList args{QStringLiteral("reverse")};
    if (noRebind)
        args << QStringLiteral("--no-rebind");
    args << remote << local;
    return runOnDevice(serial, args);
}

CommandResult Adb::removeReverse(const QString &serial, const QString &remote) const
{
    return runOnDevice(serial, {QStringLiteral("reverse"), QStringLiteral("--remove"), remote});
}

CommandResult Adb::removeAllReverses(const QString &serial) const
{
    return runOnDevice(serial, {QStringLiteral("reverse"), QStringLiteral("--remove-all")});
}

QString Adb::shellQuote(const QString &arg)
{
    QString quoted = arg;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

} // namespace QtScrcpy
