#include "testenvironment.h"

#include <qtscrcpy/adb.h>
#include <qtscrcpy/scrcpy.h>

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <csignal>
#include <sys/types.h>

using namespace QtScrcpy;

class TstScrcpy : public QObject
{
    Q_OBJECT
private slots:
    void init();

    void detectsVersion();
    void startPassesArgumentsAndAdbPath();
    void legacyVersionDropsUnsupportedOptions();
    void cameraMirroringPassesCameraArguments();
    void cameraMirroringRefusedOnOldScrcpy();
    void conflictingOptionsRefused();
    void usesAdbShippedNextToScrcpy();
    void fallsBackToTheAdbThatAdbWouldUse();
    void sameDirectoryAdbIsFoundThroughASymbolicLink();
    void anUnresolvableEnvironmentAdbIsIgnoredLikeAdbDoes();
    void explicitAdbAndEnvironmentBeatSiblingAdb();
    void streamsOutputLines();
    void stopTerminatesGracefully();
    void stopKillsAfterGracePeriod();
    void reportsExitCode();
    void refusesSecondStart();
    void missingExecutable();
    void destructorStopsProcess();

private:
    QStringList logLines() const;
    // Starts scrcpy and returns once the fake has printed its first line.
    bool startAndWaitUntilReady(Scrcpy &scrcpy, const ScrcpyOptions &options = {});

    QTemporaryDir m_dir;
    QString m_logPath;
};

static QString fakeScrcpy() { return QStringLiteral(FAKE_DIR "/scrcpy"); }

void TstScrcpy::init()
{
    QVERIFY(m_dir.isValid());
    m_logPath = m_dir.filePath("scrcpy.log");
    QFile::remove(m_logPath);
    qputenv("FAKE_SCRCPY_LOG", m_logPath.toUtf8());
    qunsetenv("FAKE_SCRCPY_VERSION");
    qunsetenv("FAKE_SCRCPY_EXIT");
    qunsetenv("FAKE_SCRCPY_IGNORE_TERM");
}

QStringList TstScrcpy::logLines() const
{
    QFile file(m_logPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
}

bool TstScrcpy::startAndWaitUntilReady(Scrcpy &scrcpy, const ScrcpyOptions &options)
{
    bool ready = false;
    connect(&scrcpy, &Scrcpy::outputReceived, this, [&ready](const QString &line, bool) {
        if (line.startsWith("INFO: Device:"))
            ready = true;
    });
    if (!scrcpy.start(options))
        return false;
    return QTest::qWaitFor([&ready] { return ready; }, 5000);
}

void TstScrcpy::detectsVersion()
{
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    QVERIFY(scrcpy.isAvailable());
    QCOMPARE(scrcpy.version(), (ScrcpyVersion{2, 4, 0}));

    qputenv("FAKE_SCRCPY_VERSION", "1.25");
    Scrcpy legacy;
    legacy.setExecutable(fakeScrcpy());
    QCOMPARE(legacy.version(), (ScrcpyVersion{1, 25, 0}));
}

void TstScrcpy::startPassesArgumentsAndAdbPath()
{
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    scrcpy.setAdbExecutable("/opt/platform-tools/adb");
    QSignalSpy started(&scrcpy, &Scrcpy::started);
    QSignalSpy states(&scrcpy, &Scrcpy::stateChanged);

    ScrcpyOptions options;
    options.serial = "SER";
    options.maxSize = 1024;
    options.windowTitle = "my phone";
    options.noAudio = true;

    QVERIFY(startAndWaitUntilReady(scrcpy, options));
    QCOMPARE(started.count(), 1);
    QCOMPARE(scrcpy.state(), Scrcpy::State::Running);
    QVERIFY(scrcpy.isRunning());
    QVERIFY(scrcpy.processId() > 0);
    QVERIFY(logLines().contains("ADB=/opt/platform-tools/adb"));

    const QStringList expected{"--serial=SER", "--max-size=1024", "--window-title=my phone", "--no-audio"};
    QCOMPARE(scrcpy.arguments(), expected);
    QCOMPARE(logLines().mid(0, 4), expected);

    scrcpy.stop();
    QTRY_COMPARE(scrcpy.state(), Scrcpy::State::Stopped);
    QCOMPARE(scrcpy.processId(), qint64(0));
    QCOMPARE(states.size(), 4); // Starting, Running, Stopping, Stopped
}

void TstScrcpy::legacyVersionDropsUnsupportedOptions()
{
    qputenv("FAKE_SCRCPY_VERSION", "1.25");
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    ScrcpyOptions options;
    options.noAudio = true;
    options.noDisplay = true;

    QVERIFY(startAndWaitUntilReady(scrcpy, options));
    QVERIFY(logLines().contains("-N"));
    QVERIFY(!logLines().contains("--no-audio"));
    scrcpy.stop();
    QTRY_COMPARE(scrcpy.state(), Scrcpy::State::Stopped);
}

void TstScrcpy::cameraMirroringPassesCameraArguments()
{
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy()); // reports 2.4
    ScrcpyOptions options = ScrcpyOptions::forCamera("SER", CameraFacing::Front);
    options.cameraFps = 30;
    options.showTouches = true; // needs control: must not reach scrcpy in camera mode

    QVERIFY(startAndWaitUntilReady(scrcpy, options));
    const QStringList expected{"--serial=SER", "--video-source=camera", "--camera-facing=front", "--camera-fps=30"};
    QCOMPARE(logLines().mid(0, 4), expected);
    QVERIFY(!logLines().contains("--show-touches"));
    scrcpy.stop();
    QTRY_COMPARE(scrcpy.state(), Scrcpy::State::Stopped);
}

void TstScrcpy::cameraMirroringRefusedOnOldScrcpy()
{
    qputenv("FAKE_SCRCPY_VERSION", "2.1");
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    QSignalSpy errors(&scrcpy, &Scrcpy::errorOccurred);

    // Falling back to the screen would silently do something else than asked for.
    QVERIFY(!scrcpy.start(ScrcpyOptions::forCamera("SER")));
    QCOMPARE(errors.count(), 1);
    QVERIFY2(errors.at(0).at(0).toString().contains("2.2 or newer (found 2.1)"),
             qPrintable(errors.at(0).at(0).toString()));
    QCOMPARE(scrcpy.state(), Scrcpy::State::Stopped);
    QVERIFY(logLines().isEmpty()); // never launched

    // The same device can still be mirrored as a screen.
    QVERIFY(startAndWaitUntilReady(scrcpy, ScrcpyOptions::forScreen("SER")));
    scrcpy.stop();
    QTRY_COMPARE(scrcpy.state(), Scrcpy::State::Stopped);
}

void TstScrcpy::conflictingOptionsRefused()
{
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    QSignalSpy errors(&scrcpy, &Scrcpy::errorOccurred);

    ScrcpyOptions options = ScrcpyOptions::forCamera("SER");
    options.cameraId = "0"; // facing defaults to Back; scrcpy rejects both together
    QVERIFY(!scrcpy.start(options));
    QCOMPARE(errors.count(), 1);
    QVERIFY(logLines().isEmpty());
}

// A directory laid out like a scrcpy release package: scrcpy and adb side by side. The lookups under
// test only accept executable files, so a TMPDIR on a noexec mount cannot be used for them.
static bool makePackageDir(const QTemporaryDir &dir)
{
    QFile::copy(fakeScrcpy(), dir.filePath("scrcpy"));
    QFile::copy(QStringLiteral(FAKE_DIR "/adb"), dir.filePath("adb"));
    return QFileInfo(dir.filePath("scrcpy")).isExecutable() && QFileInfo(dir.filePath("adb")).isExecutable();
}

void TstScrcpy::usesAdbShippedNextToScrcpy()
{
    QTemporaryDir pkg;
    if (!makePackageDir(pkg))
        QSKIP("TMPDIR does not allow executable files (noexec mount?)");
    // (the fake scrcpy is a script with an "env bash" shebang, so $PATH stays as it is)
    IsolatedProgramEnvironment isolated(QString::fromLocal8Bit(qgetenv("PATH")), pkg.path());

    Scrcpy scrcpy;
    scrcpy.setExecutable(pkg.filePath("scrcpy"));
    QCOMPARE(scrcpy.effectiveAdbExecutable(), pkg.filePath("adb"));
    QVERIFY(startAndWaitUntilReady(scrcpy));
    QVERIFY2(logLines().contains("ADB=" + pkg.filePath("adb")), qPrintable(logLines().join('|')));
    scrcpy.stop();
    QTRY_COMPARE(scrcpy.state(), Scrcpy::State::Stopped);
}

void TstScrcpy::fallsBackToTheAdbThatAdbWouldUse()
{
    // A scrcpy from a release package never searches $PATH for adb on Linux/macOS, so with no
    // adb beside it the wrapper passes on the one it would use itself.
    QTemporaryDir lonely; // only scrcpy
    QFile::copy(fakeScrcpy(), lonely.filePath("scrcpy"));
    QTemporaryDir pathDir; // a directory on $PATH that has an adb
    QFile::copy(QStringLiteral(FAKE_DIR "/adb"), pathDir.filePath("adb"));
    QTemporaryDir emptyDir;
    if (!QFileInfo(lonely.filePath("scrcpy")).isExecutable() || !QFileInfo(pathDir.filePath("adb")).isExecutable())
        QSKIP("TMPDIR does not allow executable files (noexec mount?)");

    IsolatedProgramEnvironment isolated(pathDir.path(), emptyDir.path());
    Scrcpy scrcpy;
    scrcpy.setExecutable(lonely.filePath("scrcpy"));
    QCOMPARE(scrcpy.effectiveAdbExecutable(), pathDir.filePath("adb"));

    // Nothing anywhere: left to scrcpy (and its own error message).
    qputenv("PATH", emptyDir.path().toUtf8());
    QVERIFY2(scrcpy.effectiveAdbExecutable().isEmpty(), qPrintable(scrcpy.effectiveAdbExecutable()));
}

void TstScrcpy::sameDirectoryAdbIsFoundThroughASymbolicLink()
{
    // The usual way to install the portable package: extract it, link scrcpy into a bin directory.
    // scrcpy resolves the link, so its adb is the one beside the real file.
    QTemporaryDir pkg;
    if (!makePackageDir(pkg))
        QSKIP("TMPDIR does not allow executable files (noexec mount?)");
    QTemporaryDir bin;
    QVERIFY(QFile::link(pkg.filePath("scrcpy"), bin.filePath("scrcpy")));
    QTemporaryDir emptyDir;

    IsolatedProgramEnvironment isolated(emptyDir.path(), emptyDir.path());
    Scrcpy scrcpy;
    scrcpy.setExecutable(bin.filePath("scrcpy"));
    QCOMPARE(QFileInfo(scrcpy.effectiveAdbExecutable()).canonicalFilePath(), QFileInfo(pkg.filePath("adb")).canonicalFilePath());
}

void TstScrcpy::anUnresolvableEnvironmentAdbIsIgnoredLikeAdbDoes()
{
    QTemporaryDir pkg;
    if (!makePackageDir(pkg))
        QSKIP("TMPDIR does not allow executable files (noexec mount?)");
    QTemporaryDir emptyDir;

    IsolatedProgramEnvironment isolated(emptyDir.path(), emptyDir.path());
    ScopedEnvironmentVariable staleAdb("ADB", "/nonexistent/adb");
    Scrcpy scrcpy;
    scrcpy.setExecutable(pkg.filePath("scrcpy"));
    // Scrcpy and Adb must not disagree about which adb is in use.
    QCOMPARE(scrcpy.effectiveAdbExecutable(), pkg.filePath("adb"));
    QVERIFY(Adb::findExecutable() != QStringLiteral("/nonexistent/adb"));
}

void TstScrcpy::explicitAdbAndEnvironmentBeatSiblingAdb()
{
    QTemporaryDir pkg;
    if (!makePackageDir(pkg))
        QSKIP("TMPDIR does not allow executable files (noexec mount?)");
    QTemporaryDir emptyDir;
    IsolatedProgramEnvironment isolated(emptyDir.path(), emptyDir.path());

    Scrcpy scrcpy;
    scrcpy.setExecutable(pkg.filePath("scrcpy"));

    // 1. A working $ADB is the user's choice and wins over the sibling.
    qputenv("ADB", QStringLiteral(FAKE_DIR "/adb").toUtf8());
    QCOMPARE(scrcpy.effectiveAdbExecutable(), QString()); // not overridden, scrcpy reads $ADB itself

    // 2. An explicit setAdbExecutable() wins over everything.
    scrcpy.setAdbExecutable("/explicit/adb");
    QCOMPARE(scrcpy.effectiveAdbExecutable(), QStringLiteral("/explicit/adb"));
    qunsetenv("ADB");
    QCOMPARE(scrcpy.effectiveAdbExecutable(), QStringLiteral("/explicit/adb"));
}

void TstScrcpy::streamsOutputLines()
{
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    QList<QPair<QString, bool>> lines;
    connect(&scrcpy, &Scrcpy::outputReceived, this,
            [&](const QString &line, bool isError) { lines.append({line, isError}); });

    QVERIFY(scrcpy.start({}));
    QTRY_VERIFY(lines.contains({"INFO: Device: Fake Phone", false}));
    QTRY_VERIFY(lines.contains({"WARN: something on stderr", true}));

    scrcpy.stop();
    QTRY_COMPARE(scrcpy.state(), Scrcpy::State::Stopped);
    QVERIFY(lines.contains({"INFO: bye", false})); // output written while shutting down is kept
}

void TstScrcpy::stopTerminatesGracefully()
{
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    QSignalSpy finished(&scrcpy, &Scrcpy::finished);

    QVERIFY(startAndWaitUntilReady(scrcpy));
    QCOMPARE(scrcpy.state(), Scrcpy::State::Running);
    scrcpy.stop();
    QCOMPARE(scrcpy.state(), Scrcpy::State::Stopping);
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toInt(), 0);
    QCOMPARE(finished.at(0).at(1).toBool(), false);
}

void TstScrcpy::stopKillsAfterGracePeriod()
{
    qputenv("FAKE_SCRCPY_IGNORE_TERM", "1");
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    QSignalSpy finished(&scrcpy, &Scrcpy::finished);

    QVERIFY(startAndWaitUntilReady(scrcpy));
    scrcpy.stop(300);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(scrcpy.state(), Scrcpy::State::Stopped);
    QCOMPARE(finished.at(0).at(1).toBool(), false); // killed because we asked, not a crash
}

void TstScrcpy::reportsExitCode()
{
    qputenv("FAKE_SCRCPY_EXIT", "3");
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    QSignalSpy finished(&scrcpy, &Scrcpy::finished);
    QStringList errors;
    connect(&scrcpy, &Scrcpy::outputReceived, this, [&](const QString &line, bool isError) {
        if (isError)
            errors << line;
    });

    QVERIFY(scrcpy.start({}));
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toInt(), 3);
    QCOMPARE(finished.at(0).at(1).toBool(), false);
    QCOMPARE(scrcpy.state(), Scrcpy::State::Stopped);
    QVERIFY(errors.contains("ERROR: boom"));

    // A finished instance can be started again.
    qunsetenv("FAKE_SCRCPY_EXIT");
    QVERIFY(startAndWaitUntilReady(scrcpy));
    scrcpy.stop();
    QTRY_COMPARE(scrcpy.state(), Scrcpy::State::Stopped);
}

void TstScrcpy::refusesSecondStart()
{
    Scrcpy scrcpy;
    scrcpy.setExecutable(fakeScrcpy());
    QSignalSpy errors(&scrcpy, &Scrcpy::errorOccurred);

    QVERIFY(startAndWaitUntilReady(scrcpy));
    QVERIFY(!scrcpy.start({}));
    QCOMPARE(errors.count(), 1);
    scrcpy.stop();
    QTRY_COMPARE(scrcpy.state(), Scrcpy::State::Stopped);
}

void TstScrcpy::missingExecutable()
{
    Scrcpy scrcpy;
    scrcpy.setExecutable("/nonexistent/scrcpy");
    QSignalSpy errors(&scrcpy, &Scrcpy::errorOccurred);
    QVERIFY(!scrcpy.isAvailable());
    QVERIFY(!scrcpy.version().isValid());
    QVERIFY(!scrcpy.start({}));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(scrcpy.state(), Scrcpy::State::Stopped);
}

void TstScrcpy::destructorStopsProcess()
{
    qint64 pid = 0;
    {
        Scrcpy scrcpy;
        scrcpy.setExecutable(fakeScrcpy());
        QVERIFY(startAndWaitUntilReady(scrcpy));
        pid = scrcpy.processId();
        QVERIFY(pid > 0);
    }
    QVERIFY2(::kill(static_cast<pid_t>(pid), 0) == -1, "scrcpy must not outlive its wrapper");
}

QTEST_GUILESS_MAIN(TstScrcpy)
#include "tst_scrcpy.moc"
