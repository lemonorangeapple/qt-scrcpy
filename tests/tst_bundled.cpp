// Runs next to the adb + scrcpy-server files that the build copies beside this executable and
// checks that the library finds and can use them, i.e. that an application built this way works on a
// machine without adb installed.
#include "testenvironment.h"

#include <qtscrcpy/adb.h>
#include <qtscrcpy/scrcpystream.h>

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
    void serverJarIsFound();

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
    const bool decoysOk = makeExecutableFile(m_decoys->filePath(exe("adb")));
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
#if BUNDLED_ADB
    QVERIFY2(QFileInfo::exists(inAppDir(exe("adb"))), qPrintable(exe("adb") + " missing in " + QCoreApplication::applicationDirPath()));
    QVERIFY2(QFileInfo(inAppDir(exe("adb"))).isExecutable(), "adb is not executable");
#else
    QVERIFY2(!QFileInfo::exists(inAppDir(exe("adb"))), "adb must not be bundled with QTSCRCPY_BUNDLE_ADB=OFF");
#endif
    // The scrcpy client is inside the library: no scrcpy program is shipped, only the server file that is
    // pushed to the phone.
    QVERIFY(QFileInfo::exists(inAppDir("scrcpy-server")));
    QVERIFY2(!QFileInfo::exists(inAppDir(exe("scrcpy"))), "scrcpy must not be bundled: the client is built in");
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

void TstBundled::serverJarIsFound()
{
    QCOMPARE(canonical(ScrcpyStream::findServerJar()), canonical(inAppDir("scrcpy-server")));
    QFile jar(inAppDir("scrcpy-server"));
    QVERIFY(jar.open(QIODevice::ReadOnly));
    QCOMPARE(jar.read(2), QByteArray("PK")); // a jar is a zip file
    // The server refuses a client of another version: the bundled release must be the one this client speaks.
    QCOMPARE(QString::fromLatin1(ScrcpyStream::kBundledServerVersion), QStringLiteral(EXPECTED_SCRCPY_VERSION));
}

QTEST_GUILESS_MAIN(TstBundled)
#include "tst_bundled.moc"
