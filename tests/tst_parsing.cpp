#include <qtscrcpy/adb.h>
#include <qtscrcpy/scrcpyoptions.h>

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
    void shellQuote();
    void deviceArgs();

    void version_parse_data();
    void version_parse();
    void version_ordering();

    void options_defaultsProduceNoArguments();
    void options_modernVersion();
    void options_legacyVersion();
    void options_unknownVersionIsTreatedAsModern();
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

void TstParsing::shellQuote()
{
    QCOMPARE(Adb::shellQuote(QStringLiteral("abc")), QStringLiteral("'abc'"));
    QCOMPARE(Adb::shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
    QCOMPARE(Adb::shellQuote(QStringLiteral("a b; rm -rf /")), QStringLiteral("'a b; rm -rf /'"));
    QCOMPARE(Adb::shellQuote({}), QStringLiteral("''"));
}

void TstParsing::deviceArgs()
{
    QCOMPARE(Adb::deviceArgs({}, {"devices"}), QStringList{"devices"});
    QCOMPARE(Adb::deviceArgs("SER", {"shell", "ls"}), (QStringList{"-s", "SER", "shell", "ls"}));
}

void TstParsing::version_parse_data()
{
    QTest::addColumn<QString>("output");
    QTest::addColumn<int>("major");
    QTest::addColumn<int>("minor");
    QTest::addColumn<int>("patch");
    QTest::addColumn<bool>("valid");

    QTest::newRow("1.25") << "scrcpy 1.25 <https://github.com/Genymobile/scrcpy>\n\ndependencies:" << 1 << 25 << 0 << true;
    QTest::newRow("2.4.1") << "scrcpy 2.4.1 <https://github.com/Genymobile/scrcpy>" << 2 << 4 << 1 << true;
    QTest::newRow("v-prefix") << "scrcpy v3.0" << 3 << 0 << 0 << true;
    QTest::newRow("garbage") << "command not found" << 0 << 0 << 0 << false;
    QTest::newRow("empty") << "" << 0 << 0 << 0 << false;
}

void TstParsing::version_parse()
{
    QFETCH(QString, output);
    QFETCH(int, major);
    QFETCH(int, minor);
    QFETCH(int, patch);
    QFETCH(bool, valid);

    const ScrcpyVersion version = ScrcpyVersion::parse(output);
    QCOMPARE(version.isValid(), valid);
    QCOMPARE(version.major, major);
    QCOMPARE(version.minor, minor);
    QCOMPARE(version.patch, patch);
}

void TstParsing::version_ordering()
{
    QVERIFY(ScrcpyVersion({1, 25, 0}) < ScrcpyVersion({2, 0, 0}));
    QVERIFY(ScrcpyVersion({2, 0, 0}) < ScrcpyVersion({2, 0, 1}));
    QVERIFY(ScrcpyVersion({2, 4, 0}) >= ScrcpyVersion({2, 0, 0}));
    QCOMPARE(ScrcpyVersion({2, 4, 0}).toString(), QStringLiteral("2.4"));
    QCOMPARE(ScrcpyVersion({2, 4, 1}).toString(), QStringLiteral("2.4.1"));
    QCOMPARE(ScrcpyVersion().toString(), QString());
}

void TstParsing::options_defaultsProduceNoArguments()
{
    QVERIFY(ScrcpyOptions().toArguments().isEmpty());
}

static ScrcpyOptions fullOptions()
{
    ScrcpyOptions options;
    options.serial = "SER";
    options.maxSize = 1024;
    options.bitRate = 8000000;
    options.maxFps = 60;
    options.crop = "1080:1920:0:0";
    options.windowTitle = "My phone";
    options.windowPosition = QPoint(10, 20);
    options.windowSize = QSize(540, 960);
    options.fullscreen = true;
    options.alwaysOnTop = true;
    options.borderless = true;
    options.showTouches = true;
    options.stayAwake = true;
    options.turnScreenOff = true;
    options.powerOffOnClose = true;
    options.disableScreensaver = true;
    options.noClipboardAutosync = true;
    options.noControl = true;
    options.noDisplay = true;
    options.noAudio = true;
    options.recordFile = "out.mp4";
    options.extraArguments = {"--verbosity=debug"};
    return options;
}

void TstParsing::options_modernVersion()
{
    const QStringList expected{
        "--serial=SER", "--max-size=1024", "--bit-rate=8000000", "--max-fps=60",
        "--crop=1080:1920:0:0", "--window-title=My phone",
        "--window-x=10", "--window-y=20", "--window-width=540", "--window-height=960",
        "--fullscreen", "--always-on-top", "--window-borderless",
        "--show-touches", "--stay-awake", "--turn-screen-off", "--power-off-on-close",
        "--disable-screensaver", "--no-clipboard-autosync",
        "--no-control", "-N", "--no-audio", "--record=out.mp4",
        "--verbosity=debug"};
    QCOMPARE(fullOptions().toArguments({2, 4, 0}), expected);
}

void TstParsing::options_legacyVersion()
{
    const QStringList args = fullOptions().toArguments({1, 25, 0});
    QVERIFY(!args.contains("--no-audio")); // scrcpy 1.x has no audio
    QVERIFY(args.contains("-N"));
    QVERIFY(args.contains("--serial=SER"));
}

void TstParsing::options_unknownVersionIsTreatedAsModern()
{
    ScrcpyOptions options;
    options.noAudio = true;
    QCOMPARE(options.toArguments(ScrcpyVersion()), QStringList{"--no-audio"});
}

QTEST_GUILESS_MAIN(TstParsing)
#include "tst_parsing.moc"
