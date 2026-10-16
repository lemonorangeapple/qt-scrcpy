#include <qtscrcpy/adb.h>

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QtScrcpy;

class TstAdb : public QObject
{
    Q_OBJECT
private slots:
    void init();

    void missingExecutable();
    void timeout();
    void failureIsReported();
    void version();
    void devices();
    void connectAndPair();
    void deviceInfo();
    void packages();
    void install();
    void screenshot();
    void inputTextQuoting();
    void runAsync();
    void asyncVariantsMatchBlockingOnes();
    void refreshEmitsOnlyOnChange();
    void tracking();
    void trackingWithoutAdbReportsError();
    void killServerStopsTracking();

private:
    QString logContents() const;
    void writeDevices(const QString &text) const;

    QTemporaryDir m_dir;
    QString m_logPath;
    QString m_devicesPath;
    Adb *m_adb = nullptr;
};

static const char *kTwoDevices =
    "0123456789ABCDEF       device usb:1-1 product:raven model:Pixel_6_Pro device:raven transport_id:1\n"
    "192.168.1.23:5555      device product:sdk model:Pixel_6 device:generic transport_id:2\n";

void TstAdb::init()
{
    QVERIFY(m_dir.isValid());
    m_logPath = m_dir.filePath("adb.log");
    m_devicesPath = m_dir.filePath("devices.txt");
    QFile::remove(m_logPath);
    qputenv("FAKE_ADB_LOG", m_logPath.toUtf8());
    qputenv("FAKE_ADB_DEVICES", m_devicesPath.toUtf8());
    qunsetenv("FAKE_ADB_FAIL");
    writeDevices(kTwoDevices);

    delete m_adb;
    m_adb = new Adb(QStringLiteral(FAKE_DIR "/adb"), this);
}

QString TstAdb::logContents() const
{
    QFile file(m_logPath);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void TstAdb::writeDevices(const QString &text) const
{
    QFile file(m_devicesPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text.toUtf8());
}

void TstAdb::missingExecutable()
{
    Adb adb(QStringLiteral("/nonexistent/adb"));
    QVERIFY(!adb.isAvailable());
    const CommandResult result = adb.run({"devices"});
    QVERIFY(!result.ok());
    QCOMPARE(result.error, CommandError::FailedToStart);
    QVERIFY(!result.failureReason().isEmpty());
    QVERIFY(adb.devices().isEmpty());
    QCOMPARE(adb.version(), QString());
}

void TstAdb::timeout()
{
    QElapsedTimer clock;
    clock.start();
    const CommandResult result = m_adb->run({"hang"}, 300);
    QCOMPARE(result.error, CommandError::TimedOut);
    QVERIFY(!result.ok());
    QVERIFY2(clock.elapsed() < 5000, "the hung process must be killed, not waited for");
}

void TstAdb::failureIsReported()
{
    qputenv("FAKE_ADB_FAIL", "1");
    const CommandResult result = m_adb->shell("SER", "ls");
    QVERIFY(!result.ok());
    QCOMPARE(result.exitCode, 1);
    QCOMPARE(result.failureReason(), QStringLiteral("error: no devices/emulators found"));
}

void TstAdb::version()
{
    QVERIFY(m_adb->isAvailable());
    QCOMPARE(m_adb->version(), QStringLiteral("34.0.4-fake"));
}

void TstAdb::devices()
{
    CommandResult result;
    const QList<AdbDevice> devices = m_adb->devices(&result);
    QVERIFY(result.ok());
    QCOMPARE(devices.size(), 2);
    QCOMPARE(devices[0].serial, QStringLiteral("0123456789ABCDEF"));
    QCOMPARE(devices[1].serial, QStringLiteral("192.168.1.23:5555"));
    QVERIFY(devices[1].isNetwork());
}

void TstAdb::connectAndPair()
{
    CommandResult result = m_adb->connectDevice("192.168.1.50:5555");
    QVERIFY(result.ok());
    QCOMPARE(result.text(), QStringLiteral("connected to 192.168.1.50:5555"));

    // adb exits 0 even when the connection fails; ok() must still be false.
    result = m_adb->connectDevice("bad-host:5555");
    QVERIFY(!result.ok());
    QVERIFY2(result.failureReason().contains("failed to connect"), qPrintable(result.failureReason()));

    QVERIFY(m_adb->pair("192.168.1.50:37000", "123456").ok());
    result = m_adb->pair("192.168.1.50:37000", "000000");
    QVERIFY(!result.ok());
    QVERIFY(result.failureReason().contains("Wrong password"));

    QVERIFY(m_adb->disconnectDevice("192.168.1.50:5555").ok());
}

void TstAdb::deviceInfo()
{
    QCOMPARE(m_adb->deviceIpAddress("SER"), QStringLiteral("192.168.1.23"));
    QCOMPARE(m_adb->getProp("SER", "ro.product.model"), QStringLiteral("Fake Phone"));

    const QMap<QString, QString> props = m_adb->properties("SER");
    QCOMPARE(props.size(), 2);
    QCOMPARE(props.value("ro.product.model"), QStringLiteral("Fake Phone"));
    QCOMPARE(props.value("ro.build.version.release"), QStringLiteral("14")); // CR stripped
}

void TstAdb::packages()
{
    CommandResult result;
    const QStringList packages = m_adb->packages("SER", Adb::PackageFilter::ThirdParty, &result);
    QVERIFY(result.ok());
    QCOMPARE(packages, (QStringList{"com.a.app", "com.b.app"}));
    QVERIFY(logContents().contains("pm list packages -3"));
}

void TstAdb::install()
{
    CommandResult result = m_adb->install("SER", "/tmp/good.apk");
    QVERIFY(result.ok());
    QVERIFY(logContents().contains("-s\tSER\tinstall\t-r\t/tmp/good.apk"));

    // adb install may print Failure while still exiting 0.
    result = m_adb->install("SER", "/tmp/bad.apk");
    QVERIFY(!result.ok());
    QVERIFY(result.failureReason().contains("INSTALL_FAILED_INVALID_APK"));
}

void TstAdb::screenshot()
{
    CommandResult result;
    const QByteArray png = m_adb->screenshot("SER", &result);
    QVERIFY(result.ok());
    QVERIFY(png.startsWith("\x89PNG\r\n\x1a\n")); // binary stream must arrive untouched
    QVERIFY(png.endsWith("FAKEDATA"));
}

void TstAdb::inputTextQuoting()
{
    QVERIFY(m_adb->inputText("SER", "hello it's; $(reboot)").ok());
    QVERIFY(logContents().contains(
        QStringLiteral("-s\tSER\tshell\tinput text 'hello%sit'\\''s;%s$(reboot)'\t")));

    QVERIFY(m_adb->keyEvent("SER", Adb::Key::Home).ok());
    QVERIFY(logContents().contains("shell\tinput keyevent 3\t"));
    QVERIFY(m_adb->tap("SER", 10, 20).ok());
    QVERIFY(logContents().contains("shell\tinput tap 10 20\t"));
    QVERIFY(m_adb->swipe("SER", 1, 2, 3, 4, 500).ok());
    QVERIFY(logContents().contains("shell\tinput swipe 1 2 3 4 500\t"));
}

void TstAdb::runAsync()
{
    bool called = false;
    CommandResult received;
    m_adb->runAsync({"version"}, [&](const CommandResult &result) {
        called = true;
        received = result;
    });
    QVERIFY(!called); // never invoked synchronously
    QTRY_VERIFY(called);
    QVERIFY(received.ok());
    QVERIFY(received.text().contains("34.0.4-fake"));

    // Failure to launch is reported through the callback as well.
    Adb broken(QStringLiteral("/nonexistent/adb"));
    called = false;
    broken.runAsync({"devices"}, [&](const CommandResult &result) {
        called = true;
        received = result;
    });
    QTRY_VERIFY(called);
    QCOMPARE(received.error, CommandError::FailedToStart);

    // Timeouts too.
    called = false;
    m_adb->runAsync({"hang"}, [&](const CommandResult &result) {
        called = true;
        received = result;
    }, 200);
    QTRY_VERIFY_WITH_TIMEOUT(called, 5000);
    QCOMPARE(received.error, CommandError::TimedOut);
}

void TstAdb::asyncVariantsMatchBlockingOnes()
{
    CommandResult connectBad, connectGood, installBad, installGood, shell, shot;
    int done = 0;
    const auto into = [&done](CommandResult &target) {
        return [&done, &target](const CommandResult &result) {
            target = result;
            ++done;
        };
    };

    m_adb->connectDeviceAsync("bad-host:5555", into(connectBad));
    m_adb->connectDeviceAsync("10.0.0.2:5555", into(connectGood));
    m_adb->installAsync("SER", "/tmp/bad.apk", into(installBad));
    m_adb->installAsync("SER", "/tmp/good.apk", into(installGood));
    m_adb->shellAsync("SER", "echo hi", into(shell));
    m_adb->screenshotAsync("SER", into(shot));
    QTRY_COMPARE_WITH_TIMEOUT(done, 6, 5000);

    // Same normalisation as the blocking calls: textual failures are failures.
    QVERIFY(!connectBad.ok());
    QVERIFY(connectGood.ok());
    QVERIFY(!installBad.ok());
    QVERIFY(installGood.ok());
    QCOMPARE(shell.text(), QStringLiteral("shell:echo hi"));
    QVERIFY(shot.ok());
    QVERIFY(shot.standardOutput.startsWith("\x89PNG"));
}

void TstAdb::refreshEmitsOnlyOnChange()
{
    QSignalSpy spy(m_adb, &Adb::devicesChanged);

    m_adb->refreshDevices();
    m_adb->refreshDevices(); // overlapping request is coalesced, not run in parallel
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(m_adb->lastKnownDevices().size(), 2);

    m_adb->refreshDevices();
    QTest::qWait(300);
    QCOMPARE(spy.count(), 1); // same list -> no signal

    writeDevices("ONLY_ONE            device model:Solo transport_id:9\n");
    m_adb->refreshDevices();
    QTRY_COMPARE(spy.count(), 2);
    const auto devices = spy.last().at(0).value<QList<AdbDevice>>();
    QCOMPARE(devices.size(), 1);
    QCOMPARE(devices[0].model, QStringLiteral("Solo"));
}

void TstAdb::tracking()
{
    QSignalSpy spy(m_adb, &Adb::devicesChanged);

    m_adb->startTracking();
    QVERIFY(m_adb->isTracking());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QCOMPARE(m_adb->lastKnownDevices().size(), 2);

    // A device being unplugged is picked up without any explicit refresh.
    writeDevices("0123456789ABCDEF       device usb:1-1 product:raven model:Pixel_6_Pro device:raven transport_id:1\n");
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 5000);
    QCOMPARE(m_adb->lastKnownDevices().size(), 1);

    m_adb->stopTracking();
    QVERIFY(!m_adb->isTracking());

    // Stopped means stopped.
    writeDevices("");
    QTest::qWait(400);
    QCOMPARE(spy.count(), 2);
}

void TstAdb::trackingWithoutAdbReportsError()
{
    Adb broken(QStringLiteral("/nonexistent/adb"));
    QSignalSpy errors(&broken, &Adb::errorOccurred);
    broken.startTracking();
    QTRY_VERIFY(errors.count() >= 1);
    QTRY_VERIFY(!broken.isTracking()); // gives up instead of retrying forever
}

void TstAdb::killServerStopsTracking()
{
    m_adb->startTracking();
    QVERIFY(m_adb->isTracking());
    QVERIFY(m_adb->killServer().ok());
    QVERIFY(!m_adb->isTracking());
    QVERIFY(logContents().contains("kill-server"));
}

QTEST_GUILESS_MAIN(TstAdb)
#include "tst_adb.moc"
