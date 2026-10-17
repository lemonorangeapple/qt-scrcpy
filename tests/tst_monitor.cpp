#include <qtscrcpy/devicemonitor.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QtScrcpy;

class TstMonitor : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void plugInAuthorizeUnplug();
    void alreadyConnectedDeviceIsReplayed();

private:
    void writeDevices(const QString &text) const;
    void record(DeviceMonitor &monitor);

    QTemporaryDir m_dir;
    QString m_devicesPath;
    Adb *m_adb = nullptr;
    QStringList m_events;
};

void TstMonitor::init()
{
    QVERIFY(m_dir.isValid());
    m_devicesPath = m_dir.filePath("devices.txt");
    qputenv("FAKE_ADB_DEVICES", m_devicesPath.toUtf8());
    qunsetenv("FAKE_ADB_LOG");
    qunsetenv("FAKE_ADB_FAIL");
    writeDevices({});
    m_events.clear();
    m_adb = new Adb(QStringLiteral(FAKE_DIR "/adb"), this);
}

void TstMonitor::cleanup()
{
    delete m_adb;
    m_adb = nullptr;
}

void TstMonitor::writeDevices(const QString &text) const
{
    QFile file(m_devicesPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text.toUtf8());
}

void TstMonitor::record(DeviceMonitor &monitor)
{
    connect(&monitor, &DeviceMonitor::deviceAttached, this,
            [this](const AdbDevice &d) { m_events << "attached:" + d.serial; });
    connect(&monitor, &DeviceMonitor::authorizationRequired, this,
            [this](const AdbDevice &d) { m_events << "authorization:" + d.serial; });
    connect(&monitor, &DeviceMonitor::deviceStateChanged, this,
            [this](const AdbDevice &d, AdbDevice::State) { m_events << "state:" + d.stateText; });
    connect(&monitor, &DeviceMonitor::deviceReady, this,
            [this](const AdbDevice &d) { m_events << "ready:" + d.serial + ":" + d.displayName(); });
    connect(&monitor, &DeviceMonitor::deviceDetached, this,
            [this](const AdbDevice &d) { m_events << "detached:" + d.serial; });
}

void TstMonitor::plugInAuthorizeUnplug()
{
    DeviceMonitor monitor(m_adb);
    record(monitor);
    monitor.start();

    // Cable plugged in, phone shows the "Allow USB debugging?" prompt.
    writeDevices("0123ABCD    unauthorized usb:1-1 transport_id:1\n");
    QTRY_COMPARE_WITH_TIMEOUT(m_events.size(), 2, 5000);
    QCOMPARE(m_events, (QStringList{"attached:0123ABCD", "authorization:0123ABCD"}));
    QCOMPARE(monitor.device("0123ABCD").state, AdbDevice::State::Unauthorized);

    // User taps Allow: same serial turns into a usable device, now with a model name.
    writeDevices("0123ABCD    device usb:1-1 product:raven model:Pixel_6_Pro device:raven transport_id:1\n");
    QTRY_COMPARE_WITH_TIMEOUT(m_events.size(), 4, 5000);
    QCOMPARE(m_events.mid(2), (QStringList{"state:device", "ready:0123ABCD:Pixel 6 Pro"}));
    QVERIFY(monitor.device("0123ABCD").isOnline());

    // Cable pulled.
    writeDevices({});
    QTRY_COMPARE_WITH_TIMEOUT(m_events.size(), 5, 5000);
    QCOMPARE(m_events.last(), QStringLiteral("detached:0123ABCD"));
    QVERIFY(monitor.devices().isEmpty());
    QVERIFY(monitor.device("0123ABCD").serial.isEmpty());

    monitor.stop();
}

void TstMonitor::alreadyConnectedDeviceIsReplayed()
{
    // Adb learns about the device before the monitor exists (e.g. tracking started earlier).
    writeDevices("PHONE    device model:Solo transport_id:2\n");
    QSignalSpy changed(m_adb, &Adb::devicesChanged);
    m_adb->refreshDevices();
    QTRY_COMPARE(changed.count(), 1);

    DeviceMonitor monitor(m_adb);
    record(monitor);
    monitor.start();
    QTRY_COMPARE_WITH_TIMEOUT(m_events.size(), 2, 5000);
    QCOMPARE(m_events, (QStringList{"attached:PHONE", "ready:PHONE:Solo"}));

    // The replay must not repeat once the tracker reports the same list.
    QTest::qWait(500);
    QCOMPARE(m_events.size(), 2);
    monitor.stop();
}

QTEST_GUILESS_MAIN(TstMonitor)
#include "tst_monitor.moc"
