#include "hapsigner.h"

#include "commandrunner.h"
#include "locate.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace QtScrcpy {

namespace {


const char *kToolJar = "hap-sign-tool.jar";
const char *kTemplate = "UnsgnedDebugProfileTemplate.json";
const char *kKeystore = "OpenHarmony.p12";
const char *kProfileCert = "OpenHarmonyProfileDebug.pem";
// The SDK's public debug materials (published by OpenHarmony, not a secret).
const char *kKeystorePassword = "123456";
const char *kProfileKeyAlias = "openharmony application profile debug";
const char *kAppKeyAlias = "openharmony application release";

QString tr(const char *text)
{
    return QCoreApplication::translate("QtScrcpy::HapSigner", text);
}

QString sdkToolchainsDirectories()
{
    QStringList roots;
    for (const char *variable : {"HOS_SDK_HOME", "OHOS_BASE_SDK_HOME", "DEVECO_SDK_HOME"})
        roots << qEnvironmentVariable(variable);
    const QString home = QDir::homePath();
    return roots.join(QLatin1Char(';')) + QStringLiteral(";") + home;
}

} // namespace

struct HapSigner::Impl
{
};

HapSigner::HapSigner(const QString &signingDirectory, const QString &java)
    : m_directory(signingDirectory.isEmpty() ? findSigningDirectory() : signingDirectory)
    , m_java(java.isEmpty() ? findJava() : java)
    , d(new Impl)
{
}

HapSigner::~HapSigner()
{
    delete d;
}

QString HapSigner::findSigningDirectory()
{
    const QString fromEnv = qEnvironmentVariable("QTSCRCPY_SIGNING_DIR");
    if (!fromEnv.isEmpty()) {
        if (QFileInfo(QDir(fromEnv).filePath(QString::fromLatin1(kToolJar))).isFile())
            return fromEnv;
    }

    QStringList candidates;
    if (QCoreApplication::instance())
        candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/signing");
    // An SDK that the user installed: <root>/<version>/toolchains/lib holds the signing materials.
    for (const QString &root : sdkToolchainsDirectories().split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        QDir dir(root);
        candidates << QDir(dir).filePath(QStringLiteral("lib"));
        candidates << QDir(dir).filePath(QStringLiteral("toolchains/lib"));
        for (const QString &version : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QDir sub(QDir(dir).filePath(version));
            candidates << sub.filePath(QStringLiteral("toolchains/lib"));
            for (const QString &inner : sub.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                candidates << QDir(sub.filePath(inner)).filePath(QStringLiteral("toolchains/lib"));
        }
    }
    for (const QString &candidate : candidates) {
        if (QFileInfo(QDir(candidate).filePath(QString::fromLatin1(kToolJar))).isFile())
            return QDir(candidate).absolutePath();
    }
    return {};
}

QString HapSigner::findJava()
{
    const QString home = qEnvironmentVariable("JAVA_HOME");
    if (!home.isEmpty()) {
        const QString java = QDir(home).filePath(QStringLiteral("bin/java"));
        if (!resolveExecutable(java).isEmpty())
            return resolveExecutable(java);
#ifdef Q_OS_WIN
        const QString exe = QDir(home).filePath(QStringLiteral("bin/java.exe"));
        if (QFileInfo(exe).isFile())
            return exe;
#endif
    }
#ifdef Q_OS_WIN
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("java"));
    if (!fromPath.isEmpty())
        return fromPath;
    // DevEco Studio ships a runtime that can run the signing tool.
    for (const QString &base : {qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)"),
                                qEnvironmentVariable("LOCALAPPDATA")}) {
        if (base.isEmpty())
            continue;
        const QString jbr = QDir(base).filePath(QStringLiteral("Huawei/DevEco Studio/jbr/bin/java.exe"));
        if (QFileInfo(jbr).isFile())
            return jbr;
    }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("java"));
}

QString HapSigner::path(const QString &name) const
{
    return QDir(m_directory).filePath(name);
}

QString HapSigner::checkReady() const
{
    if (m_directory.isEmpty())
        return tr("找不到 HarmonyOS 签名材料（需要签名目录里有 %1）。\n"
                  "可以从 OpenHarmony SDK 的 toolchains/lib 复制，或用 QTSCRCPY_SIGNING_DIR 指定。")
            .arg(QString::fromLatin1(kToolJar));
    if (m_java.isEmpty())
        return tr("找不到 Java 运行时，无法给手机签名。\n请安装 Java，或安装 DevEco Studio（自带 jbr），或设置 JAVA_HOME。");
    for (const char *required : {kToolJar, kTemplate, kKeystore, kProfileCert}) {
        if (!QFileInfo(path(QString::fromLatin1(required))).isFile())
            return tr("签名材料里缺少 %1（在 %2）。").arg(QString::fromLatin1(required), m_directory);
    }
    return {};
}

QString HapSigner::profileJson(const QString &udid, QString *error) const
{
    QFile file(path(QString::fromLatin1(kTemplate)));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("无法读取 %1：%2").arg(file.fileName(), file.errorString());
        return {};
    }
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = tr("%1 不是有效的 JSON：%2").arg(file.fileName(), parseError.errorString());
        return {};
    }

    QJsonObject root = document.object();
    root.insert(QStringLiteral("type"), QStringLiteral("debug"));
    root.insert(QStringLiteral("uuid"), QStringLiteral("%1-0000-4000-8000-%2")
                                            .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'))
                                            .arg(QRandomGenerator::global()->generate64() & 0xffffffffffffULL, 12, 16,
                                                 QLatin1Char('0')));
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    root.insert(QStringLiteral("validity"),
                QJsonObject{{QStringLiteral("not-before"), double(now - 300)},
                            {QStringLiteral("not-after"), double(now + 10 * 365 * 24 * 3600)}});

    QJsonObject bundle = root.value(QStringLiteral("bundle-info")).toObject();
    bundle.insert(QStringLiteral("bundle-name"), m_bundleName);
    root.insert(QStringLiteral("bundle-info"), bundle);

    QJsonObject debugInfo = root.value(QStringLiteral("debug-info")).toObject();
    debugInfo.insert(QStringLiteral("device-ids"), QJsonArray{udid});
    debugInfo.insert(QStringLiteral("device-id-type"), QStringLiteral("udid"));
    root.insert(QStringLiteral("debug-info"), debugInfo);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QStringList HapSigner::profileSignArguments(const QString &profileJsonPath, const QString &outP7b) const
{
    return {QStringLiteral("-jar"), path(QString::fromLatin1(kToolJar)),
            QStringLiteral("sign-profile"),
            QStringLiteral("-mode"), QStringLiteral("localSign"),
            QStringLiteral("-keyAlias"), QString::fromLatin1(kProfileKeyAlias),
            QStringLiteral("-keyPwd"), QString::fromLatin1(kKeystorePassword),
            QStringLiteral("-profileCertFile"), path(QString::fromLatin1(kProfileCert)),
            QStringLiteral("-inFile"), profileJsonPath,
            QStringLiteral("-signAlg"), QStringLiteral("SHA256withECDSA"),
            QStringLiteral("-keystoreFile"), path(QString::fromLatin1(kKeystore)),
            QStringLiteral("-keystorePwd"), QString::fromLatin1(kKeystorePassword),
            QStringLiteral("-outFile"), outP7b};
}

QStringList HapSigner::appSignArguments(const QString &unsignedHap, const QString &profileP7b,
                                        const QString &outHap) const
{
    return {QStringLiteral("-jar"), path(QString::fromLatin1(kToolJar)),
            QStringLiteral("sign-app"),
            QStringLiteral("-mode"), QStringLiteral("localSign"),
            QStringLiteral("-keyAlias"), QString::fromLatin1(kAppKeyAlias),
            QStringLiteral("-keyPwd"), QString::fromLatin1(kKeystorePassword),
            QStringLiteral("-appCertFile"), appCertChain(),
            QStringLiteral("-profileFile"), profileP7b,
            QStringLiteral("-inFile"), unsignedHap,
            QStringLiteral("-signAlg"), QStringLiteral("SHA256withECDSA"),
            QStringLiteral("-keystoreFile"), path(QString::fromLatin1(kKeystore)),
            QStringLiteral("-keystorePwd"), QString::fromLatin1(kKeystorePassword),
            QStringLiteral("-outFile"), outHap,
            QStringLiteral("-compatibleVersion"), QString::number(m_compatibleVersion),
            QStringLiteral("-signCode"), QStringLiteral("1")};
}

bool HapSigner::runTool(const QStringList &arguments, QString *error) const
{
    if (m_java.isEmpty()) {
        if (error)
            *error = tr("找不到 Java 运行时。");
        return false;
    }
    const CommandResult result = CommandRunner::runSync(m_java, arguments, 120 * 1000);
    if (result.ok())
        return true;
    // The tool writes its reason on stdout/stderr; the last ERROR line is the useful one.
    const QString output = QString::fromUtf8(result.standardOutput + '\n' + result.standardError);
    QString reason = result.failureReason();
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        if (line.contains(QLatin1String("ERROR")))
            reason = line.trimmed();
    }
    if (error)
        *error = reason;
    return false;
}

bool HapSigner::signProfile(const QString &profileJsonPath, const QString &outP7b, QString *error)
{
    const QString problem = checkReady();
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return false;
    }
    QFile::remove(outP7b);
    const bool ok = runTool(profileSignArguments(profileJsonPath, outP7b), error);
    if (ok && !QFileInfo::exists(outP7b)) {
        if (error)
            *error = tr("签名工具没有生成 profile 文件。");
        return false;
    }
    return ok;
}

QString HapSigner::appCertChain(QString *error) const
{
    // sign-app wants the whole certificate chain in one file (application certificate, CA, root).
    const QString shipped = path(QStringLiteral("app-cert-chain.pem"));
    if (QFileInfo(shipped).isFile())
        return shipped;

    QFile file(path(QString::fromLatin1(kTemplate)));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("无法读取 %1：%2").arg(file.fileName(), file.errorString());
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QByteArray leaf = root.value(QStringLiteral("bundle-info")).toObject()
                                .value(QStringLiteral("development-certificate")).toString().toUtf8();
    QByteArray chain = leaf;
    for (const QString &name : {QStringLiteral("OpenHarmonyApplicationCA.pem"),
                                QStringLiteral("OpenHarmonyApplicationRootCA.pem")}) {
        QFile part(path(name));
        if (leaf.isEmpty() || !part.open(QIODevice::ReadOnly)) {
            if (error)
                *error = tr("签名材料里缺少 app-cert-chain.pem（或 %1），无法组成应用证书链。").arg(name);
            return {};
        }
        chain += char(0x0A) + part.readAll();
    }
    const QString combined = QDir(m_directory).filePath(QStringLiteral("qt-scrcpy-app-cert-chain.pem"));
    QFile out(combined);
    if (!out.open(QIODevice::WriteOnly) || out.write(chain) != chain.size()) {
        if (error)
            *error = tr("无法写入 %1：%2").arg(combined, out.errorString());
        return {};
    }
    return combined;
}

bool HapSigner::signApp(const QString &unsignedHap, const QString &profileP7b, const QString &outHap, QString *error)
{
    const QString problem = checkReady();
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return false;
    }
    if (appCertChain(error).isEmpty())
        return false;

    QFile::remove(outHap);
    const bool ok = runTool(appSignArguments(unsignedHap, profileP7b, outHap), error);
    if (ok && !QFileInfo::exists(outHap)) {
        if (error)
            *error = tr("签名工具没有生成 HAP 文件。");
        return false;
    }
    return ok;
}

bool HapSigner::signForDevice(const QString &udid, const QString &unsignedHap, const QString &outHap, QString *error)
{
    if (udid.trimmed().isEmpty()) {
        if (error)
            *error = tr("没有读到设备的 UDID，无法签名。");
        return false;
    }
    QString problem = checkReady();
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return false;
    }
    if (!QFileInfo::exists(unsignedHap)) {
        if (error)
            *error = tr("找不到要签名的 HAP：%1").arg(unsignedHap);
        return false;
    }

    QString templateError;
    const QString json = profileJson(udid, &templateError);
    if (json.isEmpty()) {
        if (error)
            *error = templateError;
        return false;
    }
    const QString jsonPath = QDir(QDir::tempPath()).filePath(QStringLiteral("qt-scrcpy-profile-%1.json")
                                                                 .arg(QRandomGenerator::global()->generate()));
    QFile file(jsonPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(json.toUtf8()) != json.toUtf8().size()) {
        if (error)
            *error = tr("无法写入 %1：%2").arg(jsonPath, file.errorString());
        return false;
    }
    file.close();

    const QString p7b = jsonPath + QStringLiteral(".p7b");
    const bool ok = signProfile(jsonPath, p7b, error) && signApp(unsignedHap, p7b, outHap, error);
    QFile::remove(jsonPath);
    QFile::remove(p7b);
    return ok;
}

} // namespace QtScrcpy
