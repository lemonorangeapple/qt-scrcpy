#include <qtscrcpy/adb.h>
#include <qtscrcpy/devicemonitor.h>
#include <qtscrcpy/hdc.h>
#include <qtscrcpy/locate.h>

#include <QtTest>

using namespace QtScrcpy;

class TstParsing : public QObject
{
    Q_OBJECT
private slots:
    void parseDeviceList_realisticOutput();
    void parseDeviceList_emptyAndCrlf();
    void deviceKinds_data();
    void deviceKinds();
    void windowsCommandLineQuoting();
    void deviceArgs();
    void hdc_parseTargets();
    void hdc_parseUdid();



    void monitor_plugInAuthorizeUnplug();
    void monitor_alreadyAuthorized();
    void monitor_unrelatedChangesProduceNothing();
    void monitor_deauthorizedAgain();
    void monitor_denyThenReplug();
};

void TstParsing::parseDeviceList_realisticOutput()
{
    const QString output = QStringLiteral(
        "List of devices attached\n"
        "* daemon not running; starting now at tcp:5037\n"
        "* daemon started successfully\n"
        "0123456789ABCDEF       device usb:1-1.2 product:raven model:Pixel_6_Pro device:raven transport_id:1\n"
        "192.168.1.23:5555      device product:sdk model:Pixel_6 device:generic transport_id:2\n"
        "emulator-5554          offline transport_id:3\n"
        "R5CT1234567            unauthorized usb:1-2 transport_id:4\n"
        "ABCDEF                 no permissions (user in plugdev group; are your udev rules wrong?); "
        "see [http://developer.android.com/tools/device.html] usb:1-3 transport_id:5\n");

    const QList<AdbDevice> devices = Adb::parseDeviceList(output);
    QCOMPARE(devices.size(), 5);

    QCOMPARE(devices[0].serial, QStringLiteral("0123456789ABCDEF"));
    QCOMPARE(devices[0].state, AdbDevice::State::Device);
    QVERIFY(devices[0].isOnline());
    QCOMPARE(devices[0].usbPath, QStringLiteral("1-1.2"));
    QCOMPARE(devices[0].product, QStringLiteral("raven"));
    QCOMPARE(devices[0].model, QStringLiteral("Pixel_6_Pro"));
    QCOMPARE(devices[0].device, QStringLiteral("raven"));
    QCOMPARE(devices[0].transportId, QStringLiteral("1"));
    QCOMPARE(devices[0].displayName(), QStringLiteral("Pixel 6 Pro"));

    QCOMPARE(devices[1].serial, QStringLiteral("192.168.1.23:5555"));
    QVERIFY(devices[1].isNetwork());

    QCOMPARE(devices[2].state, AdbDevice::State::Offline);
    QVERIFY(!devices[2].isOnline());
    QVERIFY(devices[2].isEmulator());
    QCOMPARE(devices[2].displayName(), QStringLiteral("emulator-5554")); // no model -> serial
    QCOMPARE(devices[2].transportId, QStringLiteral("3"));

    QCOMPARE(devices[3].state, AdbDevice::State::Unauthorized);

    QCOMPARE(devices[4].state, AdbDevice::State::NoPermissions);
    QVERIFY(devices[4].stateText.startsWith(QLatin1String("no permissions")));
    QCOMPARE(devices[4].usbPath, QStringLiteral("1-3"));
}

void TstParsing::parseDeviceList_emptyAndCrlf()
{
    QVERIFY(Adb::parseDeviceList({}).isEmpty());
    QVERIFY(Adb::parseDeviceList(QStringLiteral("List of devices attached\n\n")).isEmpty());

    const QList<AdbDevice> devices = Adb::parseDeviceList(
        QStringLiteral("List of devices attached\r\nSERIAL1\tdevice product:p model:m device:d\r\n\r\n"));
    QCOMPARE(devices.size(), 1);
    QCOMPARE(devices[0].serial, QStringLiteral("SERIAL1"));
    QCOMPARE(devices[0].stateText, QStringLiteral("device"));
    QCOMPARE(devices[0].device, QStringLiteral("d"));
}

void TstParsing::deviceKinds_data()
{
    QTest::addColumn<QString>("serial");
    QTest::addColumn<bool>("network");
    QTest::addColumn<bool>("emulator");

    QTest::newRow("usb") << "0123456789ABCDEF" << false << false;
    QTest::newRow("emulator") << "emulator-5554" << false << true;
    QTest::newRow("ipv4") << "192.168.1.23:5555" << true << false;
    QTest::newRow("ipv6") << "[fe80::1]:5555" << true << false;
    QTest::newRow("mdns") << "adb-R5CT1234567-abcdef._adb-tls-connect._tcp" << true << false;
}

void TstParsing::deviceKinds()
{
    QFETCH(QString, serial);
    QFETCH(bool, network);
    QFETCH(bool, emulator);

    AdbDevice device;
    device.serial = serial;
    QCOMPARE(device.isNetwork(), network);
    QCOMPARE(device.isEmulator(), emulator);
}

void TstParsing::windowsCommandLineQuoting()
{
    // $ADB reaches scrcpy's Windows command line unquoted; a space would split the program path.
    QCOMPARE(quotedForWindowsCommandLine("C:/Program Files/app/adb.exe"), QStringLiteral("\"C:/Program Files/app/adb.exe\""));
    QCOMPARE(quotedForWindowsCommandLine("C:/tools/adb.exe"), QStringLiteral("C:/tools/adb.exe"));
    QCOMPARE(quotedForWindowsCommandLine("\"C:/Program Files/adb.exe\""), QStringLiteral("\"C:/Program Files/adb.exe\""));
    QCOMPARE(quotedForWindowsCommandLine({}), QString());
}

void TstParsing::deviceArgs()
{
    QCOMPARE(Adb::deviceArgs({}, {"devices"}), QStringList{"devices"});
    QCOMPARE(Adb::deviceArgs("SER", {"devices", "-l"}), (QStringList{"-s", "SER", "devices", "-l"}));
}

void TstParsing::hdc_parseTargets()
{
    const QList<AdbDevice> targets = Hdc::parseTargets(QStringLiteral(
        "7001005458323933328a01ce0f3a3900\tUSB\tConnected\tlocalhost\thdc\r\n"
        "192.168.1.5:8710\tTCP\tOffline\tlocalhost\thdc\r\n"));
    QCOMPARE(targets.size(), 2);
    QCOMPARE(targets[0].serial, QStringLiteral("7001005458323933328a01ce0f3a3900"));
    QVERIFY(targets[0].isOnline());
    QCOMPARE(targets[1].serial, QStringLiteral("192.168.1.5:8710"));
    QVERIFY(targets[1].state == AdbDevice::State::Offline);

    // Without -v only the key is printed.
    const QList<AdbDevice> plain = Hdc::parseTargets(QStringLiteral("ABC123\n"));
    QCOMPARE(plain.size(), 1);
    QVERIFY(plain[0].isOnline());

    QVERIFY(Hdc::parseTargets(QStringLiteral("[Empty]\n")).isEmpty());
    QVERIFY(Hdc::parseTargets(QStringLiteral("[Fail]ExecuteCommand need connect-key?\n")).isEmpty());
    QVERIFY(Hdc::parseTargets({}).isEmpty());
}

static AdbDevice makeDevice(const QString &serial, const QString &state, const QString &model = {})
{
    AdbDevice device;
    device.serial = serial;
    device.stateText = state;
    device.state = AdbDevice::stateFromString(state);
    device.model = model;
    return device;
}

static QList<DeviceEvent::Type> types(const QList<DeviceEvent> &events)
{
    QList<DeviceEvent::Type> result;
    for (const DeviceEvent &event : events)
        result << event.type;
    return result;
}

void TstParsing::hdc_parseUdid()
{
    // `bm get -u` prints the UDID with a label on some releases and alone on others.
    QCOMPARE(Hdc::parseUdid(QStringLiteral("udid of current device is : 0123456789ABCdef0123456789ABCDEF01\n")),
             QStringLiteral("0123456789ABCDEF0123456789ABCDEF01"));
    QCOMPARE(Hdc::parseUdid(QStringLiteral("no device")), QString());
    QCOMPARE(Hdc::parseUdid({}), QString());
}

void TstParsing::monitor_plugInAuthorizeUnplug()
{
    using T = DeviceEvent::Type;
    const AdbDevice unauthorized = makeDevice("PHONE", "unauthorized");
    const AdbDevice allowed = makeDevice("PHONE", "device", "Pixel_6"); // model is known only after Allow

    // 1. cable plugged in: the phone shows the USB debugging prompt
    QList<DeviceEvent> events = DeviceMonitor::diff({}, {unauthorized});
    QCOMPARE(types(events), (QList<T>{T::Attached, T::AuthorizationRequired}));

    // 2. user taps Allow
    events = DeviceMonitor::diff({unauthorized}, {allowed});
    QCOMPARE(types(events), (QList<T>{T::StateChanged, T::Ready}));
    QVERIFY(events[0].previousState == AdbDevice::State::Unauthorized);
    QCOMPARE(events[1].device.model, QStringLiteral("Pixel_6"));

    // 3. cable pulled
    events = DeviceMonitor::diff({allowed}, {});
    QCOMPARE(types(events), (QList<T>{T::Detached}));
    QCOMPARE(events[0].device.serial, QStringLiteral("PHONE"));
}

void TstParsing::monitor_alreadyAuthorized()
{
    using T = DeviceEvent::Type;
    // A previously authorized phone needs no prompt.
    const QList<DeviceEvent> events = DeviceMonitor::diff({}, {makeDevice("PHONE", "device")});
    QCOMPARE(types(events), (QList<T>{T::Attached, T::Ready}));
}

void TstParsing::monitor_unrelatedChangesProduceNothing()
{
    const AdbDevice a = makeDevice("A", "device", "m1");
    AdbDevice b = a;
    b.transportId = "7"; // adb renumbers transports; not worth an event
    QVERIFY(DeviceMonitor::diff({a}, {b}).isEmpty());
    QVERIFY(DeviceMonitor::diff({a}, {a}).isEmpty());
    QVERIFY(DeviceMonitor::diff({}, {}).isEmpty());
}

void TstParsing::monitor_deauthorizedAgain()
{
    using T = DeviceEvent::Type;
    // Revoking authorizations on the phone while it is connected asks again.
    const QList<DeviceEvent> events =
        DeviceMonitor::diff({makeDevice("P", "device")}, {makeDevice("P", "unauthorized")});
    QCOMPARE(types(events), (QList<T>{T::StateChanged, T::AuthorizationRequired}));
}

void TstParsing::monitor_denyThenReplug()
{
    using T = DeviceEvent::Type;
    // unauthorized -> offline -> unauthorized again must prompt each time it re-enters "unauthorized".
    const AdbDevice unauthorized = makeDevice("P", "unauthorized");
    const AdbDevice offline = makeDevice("P", "offline");
    QCOMPARE(types(DeviceMonitor::diff({unauthorized}, {offline})), (QList<T>{T::StateChanged}));
    QCOMPARE(types(DeviceMonitor::diff({offline}, {unauthorized})),
             (QList<T>{T::StateChanged, T::AuthorizationRequired}));

    // Two devices, one leaves while the other is authorized.
    const QList<DeviceEvent> events = DeviceMonitor::diff({makeDevice("A", "device"), unauthorized},
                                                          {makeDevice("A", "device"), makeDevice("P", "device")});
    QCOMPARE(types(events), (QList<T>{T::StateChanged, T::Ready}));
    QCOMPARE(events[1].device.serial, QStringLiteral("P"));
}

QTEST_GUILESS_MAIN(TstParsing)
#include "tst_parsing.moc"
