#include <qtscrcpy/adb.h>
#include <qtscrcpy/devicemonitor.h>
#include <qtscrcpy/locate.h>
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
    void windowsCommandLineQuoting();
    void deviceArgs();

    void version_parse_data();
    void version_parse();
    void version_ordering();

    void options_defaultsProduceNoArguments();
    void options_modernVersion();
    void options_legacyVersion();
    void options_unknownVersionIsTreatedAsModern();
    void options_bitRateUsesShortFlag();

    void camera_arguments();
    void camera_dropsOptionsThatNeedControl();
    void camera_idAndSizeVariants();
    void viewOnly_dropsOptionsThatNeedControl();
    void camera_settingsIgnoredForDisplay();
    void camera_validation_data();
    void camera_validation();
    void factories();

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

void TstParsing::shellQuote()
{
    QCOMPARE(Adb::shellQuote(QStringLiteral("abc")), QStringLiteral("'abc'"));
    QCOMPARE(Adb::shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
    QCOMPARE(Adb::shellQuote(QStringLiteral("a b; rm -rf /")), QStringLiteral("'a b; rm -rf /'"));
    QCOMPARE(Adb::shellQuote({}), QStringLiteral("''"));
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
    options.noDisplay = true;
    options.noAudio = true;
    options.recordFile = "out.mp4";
    options.extraArguments = {"--verbosity=debug"};
    return options;
}

void TstParsing::options_modernVersion()
{
    const QStringList expected{
        "--serial=SER", "--max-size=1024", "-b", "8000000", "--max-fps=60",
        "--crop=1080:1920:0:0", "--window-title=My phone",
        "--window-x=10", "--window-y=20", "--window-width=540", "--window-height=960",
        "--fullscreen", "--always-on-top", "--window-borderless",
        "--show-touches", "--stay-awake", "--turn-screen-off", "--power-off-on-close",
        "--disable-screensaver", "--no-clipboard-autosync",
        "-N", "--no-audio", "--record=out.mp4",
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

void TstParsing::options_bitRateUsesShortFlag()
{
    // --bit-rate was removed in scrcpy 3.0 and --video-bit-rate does not exist before 2.0;
    // -b is accepted by every release.
    ScrcpyOptions options;
    options.bitRate = 4000000;
    const QStringList expected{"-b", "4000000"};
    QCOMPARE(options.toArguments({1, 25, 0}), expected);
    QCOMPARE(options.toArguments({2, 4, 0}), expected);
    QCOMPARE(options.toArguments({3, 3, 3}), expected);
}

void TstParsing::camera_arguments()
{
    ScrcpyOptions options = ScrcpyOptions::forCamera("SER", CameraFacing::Front);
    options.maxSize = 1920;
    options.cameraAspectRatio = "16:9";
    options.cameraFps = 60;
    options.cameraHighSpeed = true;
    options.noAudio = true;

    const QStringList expected{
        "--serial=SER", "--video-source=camera", "--max-size=1920", "--camera-facing=front",
        "--camera-ar=16:9", "--camera-fps=60", "--camera-high-speed", "--no-audio"};
    QCOMPARE(options.toArguments({2, 4, 0}), expected);
    QVERIFY(options.validate({2, 4, 0}).isEmpty());
}

void TstParsing::camera_dropsOptionsThatNeedControl()
{
    // scrcpy exits with "Cannot request ... if control is disabled" for these in camera mode.
    ScrcpyOptions options = ScrcpyOptions::forCamera("SER");
    options.showTouches = true;
    options.stayAwake = true;
    options.turnScreenOff = true;
    options.powerOffOnClose = true;
    QCOMPARE(options.toArguments({3, 3, 3}),
             (QStringList{"--serial=SER", "--video-source=camera", "--camera-facing=back"}));

    // ...while the same options are kept for screen mirroring.
    options.videoSource = VideoSource::Display;
    const QStringList screen = options.toArguments({3, 3, 3});
    QVERIFY(screen.contains("--show-touches"));
    QVERIFY(screen.contains("--stay-awake"));
    QVERIFY(screen.contains("--turn-screen-off"));
    QVERIFY(screen.contains("--power-off-on-close"));
}

void TstParsing::camera_idAndSizeVariants()
{
    ScrcpyOptions byId = ScrcpyOptions::forCamera({}, CameraFacing::Any);
    byId.cameraId = "1";
    QCOMPARE(byId.toArguments({2, 4, 0}), (QStringList{"--video-source=camera", "--camera-id=1"}));

    ScrcpyOptions bySize = ScrcpyOptions::forCamera({}, CameraFacing::External);
    bySize.cameraSize = QSize(1920, 1080);
    QCOMPARE(bySize.toArguments({2, 4, 0}),
             (QStringList{"--video-source=camera", "--camera-facing=external", "--camera-size=1920x1080"}));

    // Any facing: scrcpy picks the first camera on its own.
    QCOMPARE(ScrcpyOptions::forCamera({}, CameraFacing::Any).toArguments({2, 4, 0}),
             QStringList{"--video-source=camera"});
}

void TstParsing::viewOnly_dropsOptionsThatNeedControl()
{
    ScrcpyOptions options;
    options.noControl = true;
    options.showTouches = true;
    options.stayAwake = true;
    options.turnScreenOff = true;
    options.powerOffOnClose = true;
    options.windowTitle = "t";
    QCOMPARE(options.toArguments({2, 4, 0}), (QStringList{"--window-title=t", "--no-control"}));
    QCOMPARE(options.toArguments({1, 25, 0}), (QStringList{"--window-title=t", "--no-control"}));
}

void TstParsing::camera_settingsIgnoredForDisplay()
{
    // scrcpy rejects camera options unless --video-source=camera is given.
    ScrcpyOptions options;
    options.cameraFacing = CameraFacing::Front;
    options.cameraId = "0";
    options.cameraSize = QSize(640, 480);
    options.cameraAspectRatio = "4:3";
    options.cameraFps = 30;
    options.cameraHighSpeed = true;
    QVERIFY(options.toArguments({3, 3, 3}).isEmpty());
    QVERIFY(options.validate({3, 3, 3}).isEmpty());
}

void TstParsing::camera_validation_data()
{
    QTest::addColumn<int>("scrcpyMajor");
    QTest::addColumn<int>("scrcpyMinor");
    QTest::addColumn<QString>("cameraId");
    QTest::addColumn<int>("facing");
    QTest::addColumn<bool>("withSize");
    QTest::addColumn<int>("maxSize");
    QTest::addColumn<QString>("aspectRatio");
    QTest::addColumn<QString>("errorFragment"); // empty = valid

    const int any = int(CameraFacing::Any);
    const int back = int(CameraFacing::Back);
    QTest::newRow("ok") << 2 << 4 << "" << back << false << 0 << "" << "";
    QTest::newRow("exactly 2.2") << 2 << 2 << "" << back << false << 0 << "" << "";
    QTest::newRow("2.1 has no camera") << 2 << 1 << "" << back << false << 0 << "" << "scrcpy 2.2 or newer (found 2.1)";
    QTest::newRow("1.25 has no camera") << 1 << 25 << "" << back << false << 0 << "" << "scrcpy 2.2 or newer (found 1.25)";
    QTest::newRow("id + facing") << 2 << 4 << "0" << back << false << 0 << "" << "camera id";
    QTest::newRow("id alone") << 2 << 4 << "0" << any << false << 0 << "" << "";
    QTest::newRow("size + max size") << 2 << 4 << "" << back << true << 1280 << "" << "explicit camera size";
    QTest::newRow("size + aspect ratio") << 2 << 4 << "" << back << true << 0 << "4:3" << "explicit camera size";
    QTest::newRow("max size + aspect ratio") << 2 << 4 << "" << back << false << 1280 << "4:3" << "";
}

void TstParsing::camera_validation()
{
    QFETCH(int, scrcpyMajor);
    QFETCH(int, scrcpyMinor);
    QFETCH(QString, cameraId);
    QFETCH(int, facing);
    QFETCH(bool, withSize);
    QFETCH(int, maxSize);
    QFETCH(QString, aspectRatio);
    QFETCH(QString, errorFragment);

    ScrcpyOptions options = ScrcpyOptions::forCamera("SER", static_cast<CameraFacing>(facing));
    options.cameraId = cameraId;
    if (withSize)
        options.cameraSize = QSize(1920, 1080);
    options.maxSize = maxSize;
    options.cameraAspectRatio = aspectRatio;

    const QString error = options.validate({scrcpyMajor, scrcpyMinor, 0});
    if (errorFragment.isEmpty())
        QVERIFY2(error.isEmpty(), qPrintable(error));
    else
        QVERIFY2(error.contains(errorFragment), qPrintable(error));
}

void TstParsing::factories()
{
    const ScrcpyOptions screen = ScrcpyOptions::forScreen("A");
    QCOMPARE(screen.serial, QStringLiteral("A"));
    QVERIFY(screen.videoSource == VideoSource::Display);
    QVERIFY(screen.toArguments({2, 4, 0}) == QStringList{"--serial=A"});

    const ScrcpyOptions camera = ScrcpyOptions::forCamera("B");
    QVERIFY(camera.videoSource == VideoSource::Camera);
    QVERIFY(camera.cameraFacing == CameraFacing::Back);
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
