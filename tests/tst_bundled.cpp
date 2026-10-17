// Runs next to the scrcpy/adb package that the build copies beside this executable and
// checks that the library finds and can use those copies, i.e. that an application built
// this way works on a machine without scrcpy and adb installed.
#include <qtscrcpy/adb.h>
#include <qtscrcpy/scrcpy.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QtTest>

using namespace QtScrcpy;

class TstBundled : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void packageIsNextToTheExecutable();
    void adbIsFoundAndRuns();
    void scrcpyIsFoundAndRuns();
    void scrcpyHandsTheBundledAdbToScrcpy();

private:
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
    // These would take precedence over what sits next to the executable.
    qunsetenv("ADB");
    qunsetenv("SCRCPY_EXECUTABLE");
}

void TstBundled::packageIsNextToTheExecutable()
{
    for (const QString &name : {exe("scrcpy"), exe("adb")}) {
        QVERIFY2(QFileInfo::exists(inAppDir(name)), qPrintable(name + " missing in " + QCoreApplication::applicationDirPath()));
        QVERIFY2(QFileInfo(inAppDir(name)).isExecutable(), qPrintable(name + " is not executable"));
    }
    // scrcpy pushes this file to the phone; it is looked up next to the scrcpy binary.
    QVERIFY(QFileInfo::exists(inAppDir("scrcpy-server")));
}

void TstBundled::adbIsFoundAndRuns()
{
    QCOMPARE(canonical(Adb::findExecutable()), canonical(inAppDir(exe("adb"))));

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
#ifdef Q_OS_WIN
    // scrcpy.exe finds the adb.exe in its own directory by itself (and cannot take a path with
    // spaces in $ADB), so nothing is exported.
    QCOMPARE(scrcpy.effectiveAdbExecutable(), QString());
#else
    QCOMPARE(canonical(scrcpy.effectiveAdbExecutable()), canonical(inAppDir(exe("adb"))));
#endif
}

QTEST_GUILESS_MAIN(TstBundled)
#include "tst_bundled.moc"
