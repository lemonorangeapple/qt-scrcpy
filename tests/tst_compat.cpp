// Checks that the arguments this library generates are accepted by the scrcpy that is
// actually installed (or $SCRCPY_EXECUTABLE). A fake adb without devices is used, so
// scrcpy parses and validates its command line, then stops at "no ADB device" without
// touching anything. Skipped when scrcpy is not installed.
#include <qtscrcpy/scrcpy.h>

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QtScrcpy;

class TstCompat : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void screenOptionsAreAccepted();
    void viewOnlyDropsOptionsThatNeedControl();
    void screenOptionsAreAcceptedWithoutBitRate();
    void cameraOptionsAreAccepted();
    void cameraFacingVariantsAreAccepted();

private:
    QString run(const ScrcpyOptions &options);

    QTemporaryDir m_xdg; // scrcpy insists on a usable XDG_RUNTIME_DIR
    ScrcpyVersion m_version;
};

void TstCompat::initTestCase()
{
    Scrcpy probe;
    if (!probe.isAvailable())
        QSKIP("scrcpy is not installed");
    m_version = probe.version();
    QVERIFY2(m_version.isValid(), "could not read the scrcpy version");
    qInfo() << "checking against scrcpy" << m_version.toString() << "at" << probe.executable();

    QVERIFY(m_xdg.isValid());
    qputenv("XDG_RUNTIME_DIR", m_xdg.path().toUtf8());
}

QString TstCompat::run(const ScrcpyOptions &options)
{
    Scrcpy scrcpy;
    scrcpy.setAdbExecutable(QStringLiteral(FAKE_DIR "/adb"));

    QString output;
    connect(&scrcpy, &Scrcpy::outputReceived, this,
            [&output](const QString &line, bool) { output += line + '\n'; });
    QSignalSpy finished(&scrcpy, &Scrcpy::finished);
    QSignalSpy errors(&scrcpy, &Scrcpy::errorOccurred);

    if (!scrcpy.start(options)) {
        return QStringLiteral("start refused: ") + (errors.isEmpty() ? QString() : errors.at(0).at(0).toString());
    }
    if (!finished.wait(20000)) {
        scrcpy.stop(500);
        return QStringLiteral("scrcpy did not exit; output so far:\n") + output;
    }
    return output;
}

// Anything scrcpy says when it dislikes an option, across 1.x to 3.x.
static void verifyAccepted(const QString &output, const QStringList &arguments)
{
    static const QRegularExpression complaint(
        QStringLiteral("unrecognized option|has been removed|Cannot |only available|Unexpected|Invalid|Missing|requires|Could not parse|deprecated"),
        QRegularExpression::CaseInsensitiveOption);
    const QString context = QStringLiteral("args: %1\n%2").arg(arguments.join(' '), output);

    QVERIFY2(!complaint.match(output).hasMatch(), qPrintable(context));
    // Reaching device lookup means the whole command line was parsed and validated.
    QVERIFY2(output.contains("Could not find any ADB device"), qPrintable(context));
}

static ScrcpyOptions kitchenSink()
{
    ScrcpyOptions options = ScrcpyOptions::forScreen("NOSUCHDEVICE");
    options.maxSize = 1024;
    options.bitRate = 4000000;
    options.maxFps = 30;
    options.crop = "100:100:0:0";
    options.windowTitle = "title";
    options.windowPosition = QPoint(10, 10);
    options.windowSize = QSize(300, 600);
    options.fullscreen = true;
    options.alwaysOnTop = true;
    options.borderless = true;
    options.showTouches = true;
    options.stayAwake = true;
    options.turnScreenOff = true;
    options.powerOffOnClose = true;
    options.disableScreensaver = true;
    options.noClipboardAutosync = true;
    options.noAudio = true; // dropped by the library for scrcpy < 2.0
    return options;
}

void TstCompat::screenOptionsAreAccepted()
{
    ScrcpyOptions options = kitchenSink();
    verifyAccepted(run(options), options.toArguments(m_version));
}

void TstCompat::viewOnlyDropsOptionsThatNeedControl()
{
    // With --no-control scrcpy refuses --turn-screen-off, --show-touches, ... on every release.
    ScrcpyOptions options = kitchenSink();
    options.noControl = true;
    verifyAccepted(run(options), options.toArguments(m_version));
}

void TstCompat::screenOptionsAreAcceptedWithoutBitRate()
{
    // -N needs --record on scrcpy 1.x; both together are fine everywhere.
    ScrcpyOptions options = ScrcpyOptions::forScreen("NOSUCHDEVICE");
    options.noDisplay = true;
    options.recordFile = m_xdg.filePath("out.mp4");
    verifyAccepted(run(options), options.toArguments(m_version));
}

void TstCompat::cameraOptionsAreAccepted()
{
    if (m_version < ScrcpyOptions::cameraMinimumScrcpy())
        QSKIP("this scrcpy has no camera mirroring");

    ScrcpyOptions options = ScrcpyOptions::forCamera("NOSUCHDEVICE", CameraFacing::Front);
    options.maxSize = 1280;
    options.cameraAspectRatio = "16:9";
    options.cameraFps = 30;
    options.cameraHighSpeed = true;
    options.bitRate = 4000000;
    options.noAudio = true;
    // Control-only options must be dropped, otherwise scrcpy refuses to start.
    options.showTouches = true;
    options.stayAwake = true;
    options.turnScreenOff = true;
    options.powerOffOnClose = true;
    verifyAccepted(run(options), options.toArguments(m_version));
}

void TstCompat::cameraFacingVariantsAreAccepted()
{
    if (m_version < ScrcpyOptions::cameraMinimumScrcpy())
        QSKIP("this scrcpy has no camera mirroring");

    for (CameraFacing facing : {CameraFacing::Any, CameraFacing::Front, CameraFacing::Back, CameraFacing::External}) {
        ScrcpyOptions options = ScrcpyOptions::forCamera("NOSUCHDEVICE", facing);
        verifyAccepted(run(options), options.toArguments(m_version));
    }

    ScrcpyOptions byId = ScrcpyOptions::forCamera("NOSUCHDEVICE", CameraFacing::Any);
    byId.cameraId = "0";
    byId.cameraSize = QSize(1920, 1080);
    verifyAccepted(run(byId), byId.toArguments(m_version));
}

QTEST_GUILESS_MAIN(TstCompat)
#include "tst_compat.moc"
