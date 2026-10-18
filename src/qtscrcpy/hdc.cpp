#include "hdc.h"

#include "commandrunner.h"
#include "locate.h"

#include <QDir>
#include <QFile>
#include <QPointer>
#include <QRegularExpression>

#include <utility>

namespace QtScrcpy {

namespace {

// Where the screenshot is written on the device before it is fetched.
const char kRemoteImage[] = "/data/local/tmp/qtscrcpy_snapshot.jpeg";

AdbDevice::State stateFromHdc(const QString &text)
{
    const QString lower = text.toLower();
    if (lower == QLatin1String("connected"))
        return AdbDevice::State::Device;
    if (lower == QLatin1String("unauthorized"))
        return AdbDevice::State::Unauthorized;
    if (lower == QLatin1String("offline"))
        return AdbDevice::State::Offline;
    return AdbDevice::State::Unknown;
}

// A UDID is a long hexadecimal string; older releases print it with a label, newer ones alone.
QString udidFromOutput(const QString &output)
{
    static const QRegularExpression candidate(QStringLiteral(R"([0-9A-Fa-f]{32,64})"));
    const QRegularExpressionMatch match = candidate.match(output);
    return match.hasMatch() ? match.captured(0).toUpper() : QString();
}

QString safeFileName(const QString &serial)
{
    QString name = serial;
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return name;
}

} // namespace

Hdc::Hdc(QObject *parent)
    : Hdc(findExecutable(), parent)
{
}

Hdc::Hdc(const QString &executable, QObject *parent)
    : QObject(parent)
    , m_executable(executable.isEmpty() ? QStringLiteral("hdc") : executable)
{
    qRegisterMetaType<AdbDevice>("QtScrcpy::AdbDevice");
    qRegisterMetaType<QList<AdbDevice>>("QList<QtScrcpy::AdbDevice>");
    connect(&m_pollTimer, &QTimer::timeout, this, &Hdc::refreshTargets);
}

Hdc::~Hdc() = default;

QString Hdc::findExecutable()
{
    QStringList dirs;
    for (const char *variable : {"HOS_SDK_HOME", "OHOS_BASE_SDK_HOME", "DEVECO_SDK_HOME"}) {
        const QString root = qEnvironmentVariable(variable);
        if (root.isEmpty())
            continue;
        for (const char *sub : {"toolchains", "openharmony/toolchains", "default/openharmony/toolchains"})
            dirs << QDir(root).filePath(QString::fromLatin1(sub));
    }
    return locateExecutable(QStringLiteral("hdc"), QStringLiteral("HDC"), dirs);
}

bool Hdc::isAvailable() const
{
    return !resolveExecutable(m_executable).isEmpty();
}

QList<AdbDevice> Hdc::parseTargets(const QString &output)
{
    static const QRegularExpression whitespace(QStringLiteral(R"(\s+)"));

    QList<AdbDevice> targets;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('[')))
            continue; // "[Empty]" and "[Fail]..." are not devices
        const QStringList tokens = line.split(whitespace, Qt::SkipEmptyParts);

        AdbDevice target;
        target.serial = tokens.at(0);
        // Without -v only the key is printed; such a target is reachable.
        target.stateText = tokens.size() >= 3 ? tokens.at(2) : QStringLiteral("Connected");
        target.state = stateFromHdc(target.stateText);
        targets.append(target);
    }
    return targets;
}

void Hdc::runAsync(const QStringList &args, std::function<void(const CommandResult &)> callback, int timeoutMs)
{
    auto *runner = new CommandRunner(this);
    connect(runner, &CommandRunner::finished, this,
            [callback = std::move(callback)](const CommandResult &result) {
                if (callback)
                    callback(result);
            });
    runner->start(m_executable, args, timeoutMs);
}

void Hdc::refreshTargets()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    const QPointer<Hdc> self(this);
    runAsync({QStringLiteral("list"), QStringLiteral("targets"), QStringLiteral("-v")},
             [this, self](const CommandResult &result) {
                 if (!self)
                     return;
                 m_refreshing = false;
                 if (!result.ok())
                     return; // keep what is known; the next poll tries again
                 const QList<AdbDevice> targets = parseTargets(result.text());
                 if (targets != m_targets) {
                     m_targets = targets;
                     emit targetsChanged(m_targets);
                 }
             });
}

QString Hdc::parseUdid(const QString &output)
{
    return udidFromOutput(output);
}

QString Hdc::deviceUdid(const QString &serial) const
{
    // `bm get -u` prints "udid of current device is : <64 hex>" (the wording varies by release), so the
    // UDID is picked out by shape rather than by position.
    const CommandResult result = CommandRunner::runSync(
        m_executable, {QStringLiteral("-t"), serial, QStringLiteral("shell"), QStringLiteral("bm"), QStringLiteral("get"),
                       QStringLiteral("-u")},
        kSlowCommandTimeoutMs);
    if (result.ok())
        return parseUdid(result.text());
    return {};
}

void Hdc::deviceUdidAsync(const QString &serial, std::function<void(const QString &)> callback)
{
    runAsync({QStringLiteral("-t"), serial, QStringLiteral("shell"), QStringLiteral("bm"), QStringLiteral("get"),
              QStringLiteral("-u")},
             [callback = std::move(callback)](const CommandResult &result) {
                 if (callback)
                     callback(result.ok() ? parseUdid(result.text()) : QString());
             }, kSlowCommandTimeoutMs);
}

bool Hdc::isBundleInstalled(const QString &serial, const QString &bundle) const
{
    const CommandResult result = CommandRunner::runSync(
        m_executable, {QStringLiteral("-t"), serial, QStringLiteral("shell"), QStringLiteral("bm"),
                       QStringLiteral("dump"), QStringLiteral("-n"), bundle},
        kSlowCommandTimeoutMs);
    // The command answers with the bundle's dump; anything else (an error line, or nothing at all) means
    // "not installed as far as we can tell". Requiring the bundle name keeps a silent failure from being
    // read as success.
    const QString output = result.text();
    return result.ok() && output.contains(bundle)
           && !output.contains(QLatin1String("error"), Qt::CaseInsensitive);
}

void Hdc::installAsync(const QString &serial, const QString &hapFile,
                       std::function<void(bool, const QString &)> callback)
{
    runAsync({QStringLiteral("-t"), serial, QStringLiteral("install"), QStringLiteral("-r"), QDir::toNativeSeparators(hapFile)},
             [callback = std::move(callback)](const CommandResult &result) {
                 if (!callback)
                     return;
                 const QString output = result.text();
                 // hdc reports a refused install in its output while still exiting 0.
                 const bool failed = !result.ok() || output.contains(QLatin1String("[Fail]"))
                                     || output.contains(QLatin1String("failed"), Qt::CaseInsensitive);
                 if (failed)
                     callback(false, output.isEmpty() ? result.failureReason() : output);
                 else
                     callback(true, {});
             }, 5 * 60 * 1000);
}

void Hdc::startPolling(int intervalMs)
{
    m_pollTimer.start(intervalMs);
    refreshTargets();
}

void Hdc::stopPolling()
{
    m_pollTimer.stop();
}

void Hdc::snapshotAsync(const QString &serial,
                        std::function<void(const QByteArray &image, const QString &error)> callback)
{
    const QString local = QDir::toNativeSeparators(m_tempDir.filePath(safeFileName(serial) + QStringLiteral(".jpeg")));
    QFile::remove(local);

    const QPointer<Hdc> self(this);
    const QString remote = QString::fromLatin1(kRemoteImage);
    runAsync({QStringLiteral("-t"), serial, QStringLiteral("shell"),
              QStringLiteral("snapshot_display -f ") + remote},
             [this, self, serial, local, remote, callback = std::move(callback)](const CommandResult &shot) {
                 if (!self)
                     return;
                 if (!shot.ok()) {
                     callback({}, shot.failureReason());
                     return;
                 }
                 // hdc exits with 0 even when a transfer failed, so success is judged by the file.
                 runAsync({QStringLiteral("-t"), serial, QStringLiteral("file"), QStringLiteral("recv"),
                           remote, local},
                          [local, shot, callback](const CommandResult &recv) {
                              QFile file(local);
                              if (recv.ok() && file.open(QIODevice::ReadOnly)) {
                                  const QByteArray image = file.readAll();
                                  if (!image.isEmpty()) {
                                      callback(image, {});
                                      return;
                                  }
                              }
                              QString reason = recv.failureReason();
                              if (reason.isEmpty())
                                  reason = recv.text();
                              if (reason.isEmpty())
                                  reason = shot.text();
                              callback({}, reason.isEmpty() ? QStringLiteral("no screenshot was produced") : reason);
                          });
             });
}

} // namespace QtScrcpy
