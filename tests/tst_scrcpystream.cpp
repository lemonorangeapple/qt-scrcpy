#include <qtscrcpy/adbclient.h>
#include <qtscrcpy/mirror.h>
#include <qtscrcpy/scrcpystream.h>

#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QTcpServer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QtScrcpy;

class TstScrcpyStream : public QObject
{
    Q_OBJECT
private slots:
    void serverCommandScreen();
    void serverCommandCamera();
    void serverCommandOptions();

    void adbClientQueriesTheServer();
    void adbClientPushesAFile();
    void adbClientReportsFailures();

    void streamsAScreen();
    void streamsACamera();
    void reportsAServerThatExitsAtOnce();
    void reportsAMissingJar();
    void stopEndsTheSession();
    void mirrorSessionDispatchesToAdb();
    void mirrorSessionRefusesExternalCameraOverHdc();

private:
    bool startFake(const QString &mode = QStringLiteral("ok"));
    void stopFake();
    QString writeJar();

    QProcess m_server;
    QTemporaryDir m_dir;
    int m_port = 0;
};

static int freePort()
{
    QTcpServer s;
    return s.listen(QHostAddress::LocalHost, 0) ? s.serverPort() : 0;
}

bool TstScrcpyStream::startFake(const QString &mode)
{
    stopFake();
    m_port = freePort();
    const QString state = m_dir.filePath(QStringLiteral("state-%1").arg(m_port));
    m_server.start(QStringLiteral("python"),
                   {QStringLiteral(FAKE_SERVER_SCRIPT), QString::number(m_port), QStringLiteral(SAMPLE_H264), state, mode});
    if (!m_server.waitForStarted(5000))
        return false;
    for (int i = 0; i < 100 && !QFile::exists(state + QStringLiteral("/ready")); ++i)
        QTest::qWait(50);
    qputenv("ANDROID_ADB_SERVER_PORT", QByteArray::number(m_port));
    return QFile::exists(state + QStringLiteral("/ready"));
}

void TstScrcpyStream::stopFake()
{
    if (m_server.state() != QProcess::NotRunning) {
        m_server.kill();
        m_server.waitForFinished(2000);
    }
}

QString TstScrcpyStream::writeJar()
{
    const QString path = m_dir.filePath(QStringLiteral("scrcpy-server"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write("PK fake jar");
    return path;
}

void TstScrcpyStream::serverCommandScreen()
{
    ScrcpyStream::Options o;
    o.serial = QStringLiteral("S");
    const QString cmd = ScrcpyStream::serverCommand(o, QStringLiteral("3.3.3"), 0x1a2b3c4d);
    QCOMPARE(cmd, QStringLiteral("CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process / com.genymobile.scrcpy.Server "
                                 "3.3.3 scid=1a2b3c4d log_level=info audio=false control=false tunnel_forward=true cleanup=false"));
}

void TstScrcpyStream::serverCommandCamera()
{
    ScrcpyStream::Options o;
    o.camera = ScrcpyStream::Camera::Front;
    const QString cmd = ScrcpyStream::serverCommand(o, QStringLiteral("3.3.3"), 1);
    QVERIFY(cmd.contains(QStringLiteral(" video_source=camera camera_facing=front")));
    o.camera = ScrcpyStream::Camera::External;
    QVERIFY(ScrcpyStream::serverCommand(o, QStringLiteral("3.3.3"), 1).contains(QStringLiteral("camera_facing=external")));
    o.camera = ScrcpyStream::Camera::Back;
    QVERIFY(ScrcpyStream::serverCommand(o, QStringLiteral("3.3.3"), 1).contains(QStringLiteral("camera_facing=back")));
    // The screen command carries no camera words at all.
    QVERIFY(!ScrcpyStream::serverCommand({}, QStringLiteral("3.3.3"), 1).contains(QStringLiteral("camera")));
}

void TstScrcpyStream::serverCommandOptions()
{
    ScrcpyStream::Options o;
    o.maxSize = 1280;
    o.bitRate = 4000000;
    o.maxFps = 30;
    const QString cmd = ScrcpyStream::serverCommand(o, QStringLiteral("3.3.3"), 1);
    QVERIFY(cmd.endsWith(QStringLiteral(" max_size=1280 video_bit_rate=4000000 max_fps=30")));
}

void TstScrcpyStream::adbClientQueriesTheServer()
{
    QVERIFY(startFake());
    AdbClient client;
    QString error, answer;
    QVERIFY2(client.ensureServer(&error), qPrintable(error));
    QVERIFY2(client.hostQuery(QStringLiteral("host:version"), &answer, &error), qPrintable(error));
    QCOMPARE(answer, QStringLiteral("0029"));
    stopFake();
}

void TstScrcpyStream::adbClientPushesAFile()
{
    QVERIFY(startFake());
    const QString jar = writeJar();
    AdbClient client;
    QString error;
    // Bigger than one sync chunk, so chunking is exercised.
    QFile big(m_dir.filePath(QStringLiteral("big.bin")));
    QVERIFY(big.open(QIODevice::WriteOnly));
    big.write(QByteArray(150 * 1024, 'x'));
    big.close();
    QVERIFY2(client.push(QStringLiteral("SER"), big.fileName(), QStringLiteral("/data/local/tmp/big.bin"), &error), qPrintable(error));
    QVERIFY2(client.push(QStringLiteral("SER"), jar, QStringLiteral("/data/local/tmp/scrcpy-server.jar"), &error), qPrintable(error));

    QFile log(m_dir.filePath(QStringLiteral("state-%1/adb.log").arg(m_port)));
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(log.readAll());
    QVERIFY2(text.contains(QStringLiteral("push /data/local/tmp/big.bin 153600 bytes")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("push /data/local/tmp/scrcpy-server.jar 11 bytes")), qPrintable(text));
    stopFake();
}

void TstScrcpyStream::adbClientReportsFailures()
{
    QVERIFY(startFake());
    AdbClient client;
    QString error;
    QVERIFY(!client.push(QStringLiteral("MISSING"), writeJar(), QStringLiteral("/x"), &error));
    QVERIFY2(error.contains(QStringLiteral("not found")), qPrintable(error));
    QVERIFY(!client.push(QStringLiteral("SER"), QStringLiteral("/nonexistent/file"), QStringLiteral("/x"), &error));
    QVERIFY2(error.contains(QStringLiteral("cannot read")), qPrintable(error));
    stopFake();

    // No server and no adb to start one.
    qputenv("ANDROID_ADB_SERVER_PORT", QByteArray::number(freePort()));
    AdbClient lonely;
    QVERIFY(!lonely.ensureServer(&error));
    QVERIFY2(error.contains(QStringLiteral("cannot reach")), qPrintable(error));
}

void TstScrcpyStream::streamsAScreen()
{
    if (!H264Decoder::isAvailable())
        QSKIP("built without FFmpeg");
    QVERIFY(startFake());
    ScrcpyStream::Options o;
    o.serial = QStringLiteral("SER");
    ScrcpyStream stream(o, {}, writeJar());
    int frames = 0, width = 0, height = 0;
    connect(&stream, &ScrcpyStream::frameReady, this, [&](const VideoFrame &f) { ++frames; width = f.width; height = f.height; });
    QSignalSpy finished(&stream, &ScrcpyStream::finished);

    stream.start();
    QTRY_VERIFY_WITH_TIMEOUT(frames >= 10, 15000);
    QCOMPARE(width, 160);
    QCOMPARE(height, 96);
    QCOMPARE(stream.state(), ScrcpyStream::State::Streaming);

    QFile log(m_dir.filePath(QStringLiteral("state-%1/adb.log").arg(m_port)));
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(log.readAll());
    QVERIFY(text.contains(QStringLiteral("req host:transport:SER")));
    QVERIFY(text.contains(QStringLiteral("req sync:")));
    QVERIFY(text.contains(QStringLiteral("server-cmd CLASSPATH=/data/local/tmp/scrcpy-server.jar app_process / com.genymobile.scrcpy.Server 3.3.3 ")));

    stream.stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QVERIFY(finished.at(0).at(0).toString().isEmpty());
    QCOMPARE(stream.state(), ScrcpyStream::State::Stopped);
    stopFake();
}

void TstScrcpyStream::streamsACamera()
{
    if (!H264Decoder::isAvailable())
        QSKIP("built without FFmpeg");
    QVERIFY(startFake());
    ScrcpyStream::Options o;
    o.serial = QStringLiteral("SER");
    o.camera = ScrcpyStream::Camera::Back;
    ScrcpyStream stream(o, {}, writeJar());
    int frames = 0;
    connect(&stream, &ScrcpyStream::frameReady, this, [&](const VideoFrame &) { ++frames; });
    stream.start();
    QTRY_VERIFY_WITH_TIMEOUT(frames >= 3, 15000);
    QFile log(m_dir.filePath(QStringLiteral("state-%1/adb.log").arg(m_port)));
    QVERIFY(log.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(log.readAll()).contains(QStringLiteral("video_source=camera camera_facing=back")));
    QSignalSpy finished(&stream, &ScrcpyStream::finished);
    stream.stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    stopFake();
}

void TstScrcpyStream::reportsAServerThatExitsAtOnce()
{
    QVERIFY(startFake(QStringLiteral("no-server")));
    ScrcpyStream::Options o;
    o.serial = QStringLiteral("SER");
    ScrcpyStream stream(o, {}, writeJar());
    QSignalSpy finished(&stream, &ScrcpyStream::finished);
    stream.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
    const QString error = finished.at(0).at(0).toString();
    QVERIFY2(error.contains(QStringLiteral("does not match the client")), qPrintable(error));
    QCOMPARE(stream.state(), ScrcpyStream::State::Stopped);
    stopFake();
}

void TstScrcpyStream::reportsAMissingJar()
{
    ScrcpyStream::Options o;
    ScrcpyStream stream(o, {}, QStringLiteral("/nonexistent/scrcpy-server"));
    QSignalSpy finished(&stream, &ScrcpyStream::finished);
    stream.start();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(finished.at(0).at(0).toString().contains(QStringLiteral("scrcpy-server")));
}

void TstScrcpyStream::stopEndsTheSession()
{
    if (!H264Decoder::isAvailable())
        QSKIP("built without FFmpeg");
    QVERIFY(startFake());
    ScrcpyStream::Options o;
    o.serial = QStringLiteral("SER");
    ScrcpyStream stream(o, {}, writeJar());
    QSignalSpy finished(&stream, &ScrcpyStream::finished);
    stream.start();
    QTRY_COMPARE_WITH_TIMEOUT(stream.state(), ScrcpyStream::State::Streaming, 15000);
    stream.stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QVERIFY(finished.at(0).at(0).toString().isEmpty()); // asked for: not an error
    stream.start(); // and it can be started again
    QTRY_COMPARE_WITH_TIMEOUT(stream.state(), ScrcpyStream::State::Streaming, 15000);
    stream.stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10000);
    stopFake();
}

void TstScrcpyStream::mirrorSessionDispatchesToAdb()
{
    if (!H264Decoder::isAvailable())
        QSKIP("built without FFmpeg");
    QVERIFY(startFake());
    qputenv("SCRCPY_SERVER_PATH", writeJar().toUtf8());
    MirrorOptions o;
    o.backend = Backend::Adb;
    o.serial = QStringLiteral("SER");
    o.source = MirrorSource::Screen;
    MirrorSession session(o);
    int frames = 0;
    connect(&session, &MirrorSession::frameReady, this, [&](const VideoFrame &) { ++frames; });
    QSignalSpy finished(&session, &MirrorSession::finished);
    session.start();
    QTRY_VERIFY_WITH_TIMEOUT(frames >= 5, 15000);
    QCOMPARE(session.state(), MirrorSession::State::Streaming);
    session.stop();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(session.state(), MirrorSession::State::Stopped);
    qunsetenv("SCRCPY_SERVER_PATH");
    stopFake();
}

void TstScrcpyStream::mirrorSessionRefusesExternalCameraOverHdc()
{
    MirrorOptions o;
    o.backend = Backend::Hdc;
    o.serial = QStringLiteral("X");
    o.source = MirrorSource::ExternalCamera;
    QVERIFY(!MirrorSession::unsupportedReason(o.backend, o.source).isEmpty());
    QVERIFY(MirrorSession::unsupportedReason(Backend::Adb, MirrorSource::ExternalCamera).isEmpty());

    MirrorSession session(o);
    QSignalSpy finished(&session, &MirrorSession::finished);
    session.start();
    QCOMPARE(finished.count(), 1);
    QVERIFY(!finished.at(0).at(0).toString().isEmpty());
    QCOMPARE(session.state(), MirrorSession::State::Stopped);
}

QTEST_GUILESS_MAIN(TstScrcpyStream)
#include "tst_scrcpystream.moc"
