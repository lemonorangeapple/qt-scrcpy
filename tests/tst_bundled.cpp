// Runs next to the scrcpy/adb package that the build copies beside this executable and
// checks that the library finds and can use those copies, i.e. that an application built
// this way works on a machine without scrcpy and adb installed.
#include "testenvironment.h"

#include <qtscrcpy/adb.h>
#include <qtscrcpy/scrcpy.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace QtScrcpy;

class TstBundled : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void packageIsNextToTheExecutable();
    void adbIsFoundAndRuns();
    void scrcpyIsFoundAndRuns();
    void scrcpyHandsTheBundledAdbToScrcpy();

private:
    std::unique_ptr<QTemporaryDir> m_decoys;
    std::unique_ptr<IsolatedProgramEnvironment> m_environment;
    bool m_decoysWork = false;

    static QString inAppDir(const QString &name) { return QDir(QCoreApplication::applicationDirPath()).filePath(name); }
    static QString canonical(const QString &path) { return QFileInfo(path).canonicalFilePath(); }
    static QString exe(const QString &name)
    {
#ifdef Q_OS_WIN
        return name + QStringLiteral(".exe");
#else
        return name;
#endif
    }
};

void TstBundled::initTestCase()
{
    // $ADB and $SCRCPY_EXECUTABLE would take precedence over what sits next to the executable, and
    // the machine may have no adb or scrcpy on $PATH at all, which would make "found next to the
    // program rather than on $PATH" untestable: put decoys first on $PATH.
    m_decoys = std::make_unique<QTemporaryDir>();
    const bool decoysOk = makeExecutableFile(m_decoys->filePath(exe("adb")))
                          && makeExecutableFile(m_decoys->filePath(exe("scrcpy")));
    m_environment = std::make_unique<IsolatedProgramEnvironment>(
        m_decoys->path() + QDir::listSeparator() + QString::fromLocal8Bit(qgetenv("PATH")), m_decoys->path());
    if (!decoysOk)
        qInfo("TMPDIR does not allow executable files: the precedence over $PATH is not tested");
    m_decoysWork = decoysOk;
}

void TstBundled::cleanupTestCase()
{
    m_environment.reset();
    m_decoys.reset();
}

void TstBundled::packageIsNextToTheExecutable()
{
    QStringList programs{exe("scrcpy")};
#if BUNDLED_ADB
    programs << exe("adb");
#else
    QVERIFY2(!QFileInfo::exists(inAppDir(exe("adb"))), "adb must not be bundled with QTSCRCPY_BUNDLE_ADB=OFF");
#endif
    for (const QString &name : programs) {
        QVERIFY2(QFileInfo::exists(inAppDir(name)), qPrintable(name + " missing in " + QCoreApplication::applicationDirPath()));
        QVERIFY2(QFileInfo(inAppDir(name)).isExecutable(), qPrintable(name + " is not executable"));
    }
    // scrcpy pushes this file to the phone; it is looked up next to the scrcpy binary.
    QVERIFY(QFileInfo::exists(inAppDir("scrcpy-server")));
}

void TstBundled::adbIsFoundAndRuns()
{
#if !BUNDLED_ADB
    QSKIP("adb is not bundled (QTSCRCPY_BUNDLE_ADB=OFF)");
#endif
    // The copy next to the program is found although another adb comes first on $PATH.
    QCOMPARE(canonical(Adb::findExecutable()), canonical(inAppDir(exe("adb"))));
    if (m_decoysWork)
        QVERIFY(canonical(Adb::findExecutable()) != canonical(m_decoys->filePath(exe("adb"))));

    Adb adb;
    QVERIFY(adb.isAvailable());
    // `adb version` does not start a server or touch a device.
    const QString version = adb.version();
    QVERIFY2(!version.isEmpty(), "the bundled adb did not run");
    qInfo() << "bundled adb" << version;
}

void TstBundled::scrcpyIsFoundAndRuns()
{
    QCOMPARE(canonical(Scrcpy::findExecutable()), canonical(inAppDir(exe("scrcpy"))));
    if (m_decoysWork)
        QVERIFY(canonical(Scrcpy::findExecutable()) != canonical(m_decoys->filePath(exe("scrcpy"))));

    Scrcpy scrcpy;
    QVERIFY(scrcpy.isAvailable());
    const ScrcpyVersion version = scrcpy.version();
    QVERIFY2(version.isValid(), "the bundled scrcpy did not run");
    QCOMPARE(version, ScrcpyVersion::parse(QStringLiteral("scrcpy " EXPECTED_SCRCPY_VERSION)));
}

void TstBundled::scrcpyHandsTheBundledAdbToScrcpy()
{
    // The package's scrcpy would otherwise look adb up in $PATH, where the bundled one is not.
    Scrcpy scrcpy;
#if !BUNDLED_ADB
    // No adb beside scrcpy: it is handed the one the library itself would use (here the decoy on $PATH).
    QCOMPARE(scrcpy.effectiveAdbExecutable(), Adb::findExecutable());
    if (m_decoysWork)
        QVERIFY(!scrcpy.effectiveAdbExecutable().isEmpty());
#elif defined(Q_OS_WIN)
    // scrcpy.exe finds the adb.exe in its own directory by itself (and cannot take a path with
    // spaces in $ADB), so nothing is exported.
    QCOMPARE(scrcpy.effectiveAdbExecutable(), QString());
#else
    QCOMPARE(canonical(scrcpy.effectiveAdbExecutable()), canonical(inAppDir(exe("adb"))));
#endif
}

QTEST_GUILESS_MAIN(TstBundled)
#include "tst_bundled.moc"
