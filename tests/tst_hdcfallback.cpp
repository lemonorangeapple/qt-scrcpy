// The screenshot fallback: when the companion application cannot be installed on a HarmonyOS device,
// the screen must still be shown through the system screenshot command (low frame rate, screen only).
#include <qtscrcpy/hdc.h>
#include <qtscrcpy/hdcstream.h>
#include <qtscrcpy/imagedecoder.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QtScrcpy;

class TstHdcFallback : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void fallsBackWhenTheDeviceRefusesTheInstall();
    void reportsInsteadOfFallingBackForACamera();
    void stopsCleanlyWhileInScreenshotMode();
    void refusesWithoutTheScreenshotTool();

private:
    QString writeFakeHap() const;

    QTemporaryDir m_dir;
    Hdc *m_hdc = nullptr;
    QString m_logPath;
};

void TstHdcFallback::init()
{
    QVERIFY(m_dir.isValid());
    m_logPath = m_dir.filePath(QStringLiteral("hdc.log"));
    QFile::remove(m_logPath);
    qputenv("FAKE_HDC_LOG", m_logPath.toUtf8());
    qputenv("FAKE_HDC_RECV", QByteArray(SAMPLE_JPEG));
    qputenv("FAKE_HDC_DUMP", "missing");     // the companion app is not installed
    qputenv("FAKE_HDC_INSTALL", "fail");     // and the device refuses our package
    qputenv("FAKE_HDC_FPORT", "fail");
    m_hdc = new Hdc(QStringLiteral(FAKE_HDC_PATH));
}

void TstHdcFallback::cleanup()
{
    delete m_hdc;
    m_hdc = nullptr;
}

QString TstHdcFallback::writeFakeHap() const
{
    const QString path = m_dir.filePath(QStringLiteral("app.hap"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write("PK not really a package");
    return path;
}

void TstHdcFallback::fallsBackWhenTheDeviceRefusesTheInstall()
{
    if (!ImageDecoder::isAvailable())
        QSKIP("built without a JPEG decoder");
    HdcStream stream(m_hdc, QStringLiteral("FAKEUDID0001"), StreamProtocol::Source::Screen);
    // A package that is already signed: the library installs it as it is, the device refuses it.
    stream.setHapToInstall(writeFakeHap(), false);
    stream.setConnectTimeoutMs(30000);

    QSignalSpy notices(&stream, &HdcStream::notice);
    QSignalSpy finished(&stream, &HdcStream::finished);
    int frames = 0, width = 0, height = 0;
    connect(&stream, &HdcStream::frameReady, this, [&](const VideoFrame &frame) {
        ++frames;
        width = frame.width;
        height = frame.height;
    });

    stream.start();
    QTRY_VERIFY_WITH_TIMEOUT(frames >= 3, 30000);
    QCOMPARE(width, 160);
    QCOMPARE(height, 96);
    QCOMPARE(stream.mode(), HdcStream::Mode::Screenshots);
    QCOMPARE(stream.state(), HdcStream::State::Streaming);
    QCOMPARE(finished.count(), 0);
    // The user must be told why the picture is now slow.
    QVERIFY(notices.count() >= 1);
    const QString notice = notices.at(0).at(0).toString();
    QVERIFY2(notice.contains(QStringLiteral("截图")), qPrintable(notice));
    QVERIFY2(notice.contains(QStringLiteral("拒绝")), qPrintable(notice));

    // The device really was asked for a picture, and the frames kept coming.
    QFile log(m_logPath);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QString calls = QString::fromUtf8(log.readAll());
    QVERIFY2(calls.contains(QStringLiteral("install -r")), qPrintable(calls));
    QVERIFY2(calls.contains(QStringLiteral("snapshot_display -f")), qPrintable(calls));
    QVERIFY2(calls.contains(QStringLiteral("file recv")), qPrintable(calls));

    stream.stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QVERIFY(finished.at(0).at(0).toString().isEmpty()); // asked for: not an error
}

void TstHdcFallback::reportsInsteadOfFallingBackForACamera()
{
    HdcStream stream(m_hdc, QStringLiteral("FAKEUDID0001"), StreamProtocol::Source::BackCamera);
    stream.setHapToInstall(writeFakeHap(), false); // the device refuses it

    QSignalSpy finished(&stream, &HdcStream::finished);
    QSignalSpy notices(&stream, &HdcStream::notice);
    stream.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    // A camera cannot be read with a screenshot, so this must be an error, not a silent fallback.
    QVERIFY(!finished.at(0).at(0).toString().isEmpty());
    QVERIFY2(finished.at(0).at(0).toString().contains(QStringLiteral("拒绝")),
             qPrintable(finished.at(0).at(0).toString()));
    QCOMPARE(notices.count(), 0);
    QCOMPARE(stream.mode(), HdcStream::Mode::Streamer);
    QCOMPARE(stream.state(), HdcStream::State::Stopped);
}

void TstHdcFallback::stopsCleanlyWhileInScreenshotMode()
{
    if (!ImageDecoder::isAvailable())
        QSKIP("built without a JPEG decoder");
    HdcStream stream(m_hdc, QStringLiteral("FAKEUDID0001"), StreamProtocol::Source::Screen);
    stream.setHapToInstall(writeFakeHap(), false);
    int frames = 0;
    connect(&stream, &HdcStream::frameReady, this, [&](const VideoFrame &) { ++frames; });
    QSignalSpy finished(&stream, &HdcStream::finished);

    stream.start();
    QTRY_VERIFY_WITH_TIMEOUT(frames >= 2, 30000);
    stream.stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    const int seenAtStop = frames;
    // No picture may arrive after the session ended (a queued snapshot must not restart the loop).
    QTest::qWait(400);
    QCOMPARE(frames, seenAtStop);
    QCOMPARE(stream.state(), HdcStream::State::Stopped);
}

void TstHdcFallback::refusesWithoutTheScreenshotTool()
{
    // A device that answers every call but never produces a picture: the session must give up with a
    // message instead of spinning forever.
    qputenv("FAKE_HDC_RECV", QByteArray("/nonexistent/picture.jpg"));
    HdcStream stream(m_hdc, QStringLiteral("FAKEUDID0001"), StreamProtocol::Source::Screen);
    stream.setHapToInstall(writeFakeHap(), false);
    QSignalSpy finished(&stream, &HdcStream::finished);
    stream.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
    QVERIFY2(finished.at(0).at(0).toString().contains(QStringLiteral("截图")),
             qPrintable(finished.at(0).at(0).toString()));
}

QTEST_GUILESS_MAIN(TstHdcFallback)
#include "tst_hdcfallback.moc"
