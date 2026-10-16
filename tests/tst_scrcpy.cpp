#include <qtscrcpy/scrcpy.h>

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
