// Checks the HarmonyOS signing step: the profile built for a device, and (when Java and the SDK's
// signing materials are available) a real sign + verify of a small HAP.
#include <qtscrcpy/hapsigner.h>
#include <qtscrcpy/hdc.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace QtScrcpy;

class TstSigning : public QObject
{
    Q_OBJECT
private slots:
    void udidIsPickedOutOfTheAnswer_data();
    void udidIsPickedOutOfTheAnswer();
    void profileCarriesTheDeviceAndBundle();
    void profileValidityIsFresh();
    void signingArguments();
    void signsAndVerifiesARealHap();
    void refusesWithoutMaterials();
    void findsMaterialsAndJava();

private:
    static QString writeFakeHap(const QString &directory);
};

void TstSigning::udidIsPickedOutOfTheAnswer_data()
{
    QTest::addColumn<QString>("output");
    QTest::addColumn<QString>("expected");
    const QString udid = QStringLiteral("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
    QTest::newRow("labelled") << QStringLiteral("udid of current device is : %1\n").arg(udid) << udid;
    QTest::newRow("bare") << udid + QStringLiteral("\n") << udid;
    QTest::newRow("crlf") << QStringLiteral("UDID: %1\r\n").arg(udid) << udid;
    QTest::newRow("short-32") << QStringLiteral("udid = 0123456789abcdef0123456789abcdef")
                             << QStringLiteral("0123456789ABCDEF0123456789ABCDEF");
    QTest::newRow("nothing") << QStringLiteral("error: no device\n") << QString();
    QTest::newRow("empty") << QString() << QString();
}

void TstSigning::udidIsPickedOutOfTheAnswer()
{
    QFETCH(QString, output);
    QFETCH(QString, expected);
    QCOMPARE(Hdc::parseUdid(output), expected);
}

void TstSigning::profileCarriesTheDeviceAndBundle()
{
    HapSigner signer(QStringLiteral(SIGNING_DIR));
    if (!signer.checkReady().isEmpty())
        QSKIP("no signing materials next to the tests");
    const QString udid = QStringLiteral("AABBCCDDEEFF00112233445566778899AABBCCDDEEFF00112233445566778899");
    QString error;
    const QString json = signer.profileJson(udid, &error);
    QVERIFY2(!json.isEmpty(), qPrintable(error));

    const QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
    QCOMPARE(root.value(QStringLiteral("type")).toString(), QStringLiteral("debug"));
    const QJsonObject bundle = root.value(QStringLiteral("bundle-info")).toObject();
    QCOMPARE(bundle.value(QStringLiteral("bundle-name")).toString(), QStringLiteral("com.qtscrcpy.streamer"));
    // The template's application certificate must survive: the device checks it against the HAP's.
    QVERIFY(bundle.value(QStringLiteral("development-certificate")).toString()
                .startsWith(QStringLiteral("-----BEGIN CERTIFICATE-----")));
    const QJsonObject debug = root.value(QStringLiteral("debug-info")).toObject();
    QCOMPARE(debug.value(QStringLiteral("device-id-type")).toString(), QStringLiteral("udid"));
    QCOMPARE(debug.value(QStringLiteral("device-ids")).toArray().at(0).toString(), udid);

    signer.setBundleName(QStringLiteral("com.example.other"));
    const QJsonObject other = QJsonDocument::fromJson(signer.profileJson(udid, &error).toUtf8()).object();
    QCOMPARE(other.value(QStringLiteral("bundle-info")).toObject().value(QStringLiteral("bundle-name")).toString(),
             QStringLiteral("com.example.other"));
    QVERIFY(other.value(QStringLiteral("uuid")).toString() != root.value(QStringLiteral("uuid")).toString());
}

void TstSigning::profileValidityIsFresh()
{
    HapSigner signer(QStringLiteral(SIGNING_DIR));
    if (!signer.checkReady().isEmpty())
        QSKIP("no signing materials next to the tests");
    const QJsonObject validity = QJsonDocument::fromJson(signer.profileJson(QStringLiteral("AA"), nullptr).toUtf8())
                                     .object().value(QStringLiteral("validity")).toObject();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // The SDK template expired in 2024; the profile must be valid now and for years, not hours.
    const qint64 before = qint64(validity.value(QStringLiteral("not-before")).toDouble());
    const qint64 after = qint64(validity.value(QStringLiteral("not-after")).toDouble());
    QVERIFY2(before <= now, qPrintable(QString::number(before)));
    QVERIFY2(after > now + 24 * 3600, qPrintable(QString::number(after)));
}

void TstSigning::signingArguments()
{
    HapSigner signer(QStringLiteral(SIGNING_DIR));
    const QStringList profile = signer.profileSignArguments(QStringLiteral("in.json"), QStringLiteral("out.p7b"));
    QVERIFY(profile.contains(QStringLiteral("sign-profile")));
    QVERIFY(profile.contains(QStringLiteral("openharmony application profile debug")));
    QVERIFY(profile.endsWith(QStringLiteral("out.p7b")));
    QVERIFY(profile.join(QLatin1Char(' ')).contains(QStringLiteral("OpenHarmonyProfileDebug.pem")));

    const QStringList app =
        signer.appSignArguments(QStringLiteral("u.hap"), QStringLiteral("p.p7b"), QStringLiteral("o.hap"));
    QVERIFY(app.contains(QStringLiteral("sign-app")));
    QVERIFY(app.contains(QStringLiteral("openharmony application release")));
    QVERIFY(app.join(QLatin1Char(' ')).contains(QStringLiteral("app-cert-chain.pem")));
    QVERIFY(app.join(QLatin1Char(' ')).contains(QStringLiteral("-signCode 1")));
}

QString TstSigning::writeFakeHap(const QString &directory)
{
    // A HAP is a zip. CMake is guaranteed to be around (it built this test) and can write one without
    // depending on PowerShell, tar or zip being installed.
    const QString path = directory + QStringLiteral("/unsigned.hap");
    QProcess zip;
    zip.setWorkingDirectory(directory + QStringLiteral("/hap-in"));
    zip.start(QStringLiteral(CMAKE_COMMAND),
              {QStringLiteral("-E"), QStringLiteral("tar"), QStringLiteral("cf"), path, QStringLiteral("--format=zip"),
               QStringLiteral(".")});
    if (!zip.waitForFinished(60000) || zip.exitCode() != 0)
        return {};
    return path;
}

void TstSigning::signsAndVerifiesARealHap()
{
    HapSigner signer(QStringLiteral(SIGNING_DIR));
    const QString problem = signer.checkReady();
    if (!problem.isEmpty())
        QSKIP(qPrintable(QStringLiteral("signing is not possible here: ") + problem));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("hap-in"))));
    const QJsonObject module{
        {QStringLiteral("app"), QJsonObject{{QStringLiteral("bundleName"), QStringLiteral("com.qtscrcpy.streamer")}}},
        {QStringLiteral("module"), QJsonObject{{QStringLiteral("name"), QStringLiteral("entry")}}}};
    QFile moduleFile(dir.filePath(QStringLiteral("hap-in/module.json")));
    QVERIFY(moduleFile.open(QIODevice::WriteOnly));
    moduleFile.write(QJsonDocument(module).toJson());
    moduleFile.close();
    QFile other(dir.filePath(QStringLiteral("hap-in/pack.info")));
    QVERIFY(other.open(QIODevice::WriteOnly));
    other.write("{}");
    other.close();

    const QString unsignedHap = writeFakeHap(dir.path());
    QVERIFY2(!unsignedHap.isEmpty() && QFileInfo::exists(unsignedHap), "could not build the test package");

    const QString udid = QStringLiteral("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
    const QString signedHap = dir.filePath(QStringLiteral("signed.hap"));
    QString error;
    QVERIFY2(signer.signForDevice(udid, unsignedHap, signedHap, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(signedHap));
    QVERIFY2(QFileInfo(signedHap).size() > QFileInfo(unsignedHap).size(), "the signature block is missing");

    // The tool must accept what we produced: that is the check the device does first.
    QProcess verify;
    verify.start(signer.java(),
                 {QStringLiteral("-jar"), signer.signingDirectory() + QStringLiteral("/hap-sign-tool.jar"),
                  QStringLiteral("verify-app"), QStringLiteral("-inFile"), signedHap,
                  QStringLiteral("-outCertChain"), dir.filePath(QStringLiteral("chain.cer")),
                  QStringLiteral("-outProfile"), dir.filePath(QStringLiteral("profile.p7b"))});
    QVERIFY(verify.waitForFinished(120000));
    const QString output = QString::fromUtf8(verify.readAllStandardOutput() + verify.readAllStandardError());
    QVERIFY2(output.contains(QStringLiteral("success"), Qt::CaseInsensitive), qPrintable(output));
    QVERIFY2(!output.contains(QStringLiteral("ERROR")), qPrintable(output));
}

void TstSigning::refusesWithoutMaterials()
{
    HapSigner signer(QStringLiteral("/nonexistent/signing"), QStringLiteral("/nonexistent/java"));
    QVERIFY(!signer.checkReady().isEmpty());
    QString error;
    QVERIFY(!signer.signForDevice(QStringLiteral("AA"), QStringLiteral("/nonexistent/a.hap"),
                                  QStringLiteral("/nonexistent/b.hap"), &error));
    QVERIFY(!error.isEmpty());

    // An empty UDID is refused before anything else.
    HapSigner real(QStringLiteral(SIGNING_DIR));
    QVERIFY(!real.signForDevice({}, QStringLiteral("/nonexistent/a.hap"), QStringLiteral("/nonexistent/b.hap"), &error));
    QVERIFY(error.contains(QStringLiteral("UDID")));
}

void TstSigning::findsMaterialsAndJava()
{
    // Both lookups must work even when told nothing, even when nothing is installed.
    const QString dir = HapSigner::findSigningDirectory();
    const QString java = HapSigner::findJava();
    QVERIFY(dir.isEmpty() || QFileInfo(QDir(dir).filePath(QStringLiteral("hap-sign-tool.jar"))).isFile());
    QVERIFY(java.isEmpty() || QFileInfo(java).isFile());
}

QTEST_GUILESS_MAIN(TstSigning)
#include "tst_signing.moc"
