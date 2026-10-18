#include <qtscrcpy/h264decoder.h>
#include <qtscrcpy/streamprotocol.h>

#include <QFile>
#include <QtTest>

using namespace QtScrcpy;
using namespace QtScrcpy::StreamProtocol;

class TstStream : public QObject
{
    Q_OBJECT
private slots:
    void headerRoundTrip();
    void packetRoundTrip();
    void parserHandlesByteByByteInput();
    void parserHandlesSeveralItemsInOneChunk();
    void parserRejectsBadMagic();
    void parserRejectsOversizedPacket();
    void parserRejectsUnknownPacketType();

    void decoderDecodesRealH264();
    void decoderSurvivesGarbage();
    void decoderWaitsForKeyFrameAfterReset();

private:
    static QByteArray sample();
    // Splits the Annex B sample into access units (one per picture, parameter sets stay with their IDR).
    static QList<QByteArray> accessUnits(const QByteArray &annexB);
};

QByteArray TstStream::sample()
{
    QFile file(QStringLiteral(SAMPLE_H264));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QList<QByteArray> TstStream::accessUnits(const QByteArray &d)
{
    // The encoder emits one slice per picture; SPS/PPS (types 7, 8) belong to the IDR slice that follows.
    QList<int> starts;
    for (int i = 0; i + 3 < d.size(); ++i) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1)
            starts << (i > 0 && d[i - 1] == 0 ? i - 1 : i);
    }
    QList<QByteArray> units;
    QByteArray current;
    for (int k = 0; k < starts.size(); ++k) {
        const int end = k + 1 < starts.size() ? starts[k + 1] : d.size();
        const QByteArray nal = d.mid(starts[k], end - starts[k]);
        const int type = nal[nal.indexOf(char(1)) + 1] & 0x1f;
        current += nal;
        if (type == 1 || type == 5) { // a slice completes the picture
            units << current;
            current.clear();
        }
    }
    return units;
}

void TstStream::headerRoundTrip()
{
    Header header;
    header.source = Source::FrontCamera;
    header.width = 1080;
    header.height = 2400;
    const QByteArray bytes = encodeHeader(header);
    QCOMPARE(bytes.size(), kHeaderSize);
    QVERIFY(bytes.startsWith("QSC1"));

    Parser parser;
    parser.append(bytes);
    Header parsed;
    QVERIFY(parser.next(&parsed, nullptr) == Parser::Status::Header);
    QVERIFY(parsed.source == Source::FrontCamera);
    QCOMPARE(parsed.width, 1080u);
    QCOMPARE(parsed.height, 2400u);
    QVERIFY(parser.next(nullptr, nullptr) == Parser::Status::NeedMore);
}

void TstStream::packetRoundTrip()
{
    Packet packet;
    packet.type = PacketType::KeyFrame;
    packet.timestampUs = 0x0123456789abcdefULL;
    packet.payload = QByteArray("\x00\x00\x00\x01\x65hello", 10);

    Parser parser;
    parser.append(encodeHeader({}));
    parser.append(encodePacket(packet));
    QVERIFY(parser.next(nullptr, nullptr) == Parser::Status::Header);
    Packet parsed;
    QVERIFY(parser.next(nullptr, &parsed) == Parser::Status::Packet);
    QVERIFY(parsed.type == PacketType::KeyFrame);
    QCOMPARE(parsed.timestampUs, packet.timestampUs);
    QCOMPARE(parsed.payload, packet.payload);
}

void TstStream::parserHandlesByteByByteInput()
{
    Packet packet;
    packet.type = PacketType::Frame;
    packet.timestampUs = 42;
    packet.payload = QByteArray(300, 'x');
    const QByteArray all = encodeHeader({}) + encodePacket(packet) + encodePacket(packet);

    Parser parser;
    int headers = 0, packets = 0;
    for (char byte : all) {
        parser.append(QByteArray(1, byte));
        while (true) {
            Packet parsed;
            const auto status = parser.next(nullptr, &parsed);
            if (status == Parser::Status::NeedMore)
                break;
            QVERIFY2(status != Parser::Status::Error, qPrintable(parser.errorString()));
            if (status == Parser::Status::Header)
                ++headers;
            else {
                ++packets;
                QCOMPARE(parsed.payload.size(), 300);
            }
        }
    }
    QCOMPARE(headers, 1);
    QCOMPARE(packets, 2);
}

void TstStream::parserHandlesSeveralItemsInOneChunk()
{
    Packet a, b;
    a.type = PacketType::Config;
    a.payload = "sps";
    b.type = PacketType::Frame;
    b.payload = "slice";
    Parser parser;
    parser.append(encodeHeader({}) + encodePacket(a) + encodePacket(b));
    QVERIFY(parser.next(nullptr, nullptr) == Parser::Status::Header);
    Packet p;
    QVERIFY(parser.next(nullptr, &p) == Parser::Status::Packet);
    QCOMPARE(p.payload, QByteArray("sps"));
    QVERIFY(parser.next(nullptr, &p) == Parser::Status::Packet);
    QCOMPARE(p.payload, QByteArray("slice"));
    QVERIFY(parser.next(nullptr, &p) == Parser::Status::NeedMore);
}

void TstStream::parserRejectsBadMagic()
{
    Parser parser;
    parser.append(QByteArray("HTTP/1.1 200 OK\r\n\r\n"));
    QVERIFY(parser.next(nullptr, nullptr) == Parser::Status::Error);
    QVERIFY(!parser.errorString().isEmpty());
    QVERIFY(parser.next(nullptr, nullptr) == Parser::Status::Error); // stays failed
}

void TstStream::parserRejectsOversizedPacket()
{
    QByteArray bad = encodeHeader({});
    bad += QByteArray("\x02\x00\x00\x00", 4) + QByteArray(8, '\0') + QByteArray("\x7f\xff\xff\xff", 4);
    Parser parser;
    parser.append(bad);
    QVERIFY(parser.next(nullptr, nullptr) == Parser::Status::Header);
    QVERIFY(parser.next(nullptr, nullptr) == Parser::Status::Error);
    QVERIFY(parser.errorString().contains("too large"));
}

void TstStream::parserRejectsUnknownPacketType()
{
    QByteArray bad = encodeHeader({});
    bad += QByteArray("\x09\x00\x00\x00", 4) + QByteArray(12, '\0');
    Parser parser;
    parser.append(bad);
    QVERIFY(parser.next(nullptr, nullptr) == Parser::Status::Header);
    QVERIFY(parser.next(nullptr, nullptr) == Parser::Status::Error);
}

void TstStream::decoderDecodesRealH264()
{
    if (!H264Decoder::isAvailable())
        QSKIP("built without FFmpeg");
    const QByteArray data = sample();
    QVERIFY2(!data.isEmpty(), "tests/data/sample.h264 is missing");

    H264Decoder decoder;
    QVERIFY2(decoder.isUsable(), qPrintable(decoder.errorString()));

    const QList<QByteArray> units = accessUnits(data);
    QVERIFY2(units.size() >= 10, qPrintable(QString::number(units.size())));
    int pictures = 0;
    for (const QByteArray &unit : units) {
        const QList<VideoFrame> frames = decoder.decode(unit);
        for (const VideoFrame &frame : frames) {
            QCOMPARE(frame.width, 160);
            QCOMPARE(frame.height, 96);
            QCOMPARE(frame.bgra.size(), 160 * 96 * 4);
            // The ffmpeg "testsrc" pattern is colourful: an all-black or all-equal picture means the
            // conversion is broken.
            QVERIFY(frame.bgra.count(char(0)) < frame.bgra.size() / 2);
            QCOMPARE(quint8(frame.bgra[3]), quint8(255)); // alpha
            ++pictures;
        }
    }
    // Low-delay decoding: every access unit yields its picture, no reordering delay.
    QCOMPARE(pictures, units.size());
}

void TstStream::decoderSurvivesGarbage()
{
    if (!H264Decoder::isAvailable())
        QSKIP("built without FFmpeg");
    H264Decoder decoder;
    QVERIFY(decoder.isUsable());
    QVERIFY(decoder.decode(QByteArray(1000, char(0x5a))).isEmpty());
    QVERIFY(decoder.decode(QByteArray("\x00\x00\x00\x01\x65\xff\xff", 7)).isEmpty());
    QVERIFY(decoder.decode({}).isEmpty());

    // ...and still works afterwards.
    int pictures = 0;
    for (const QByteArray &unit : accessUnits(sample()))
        pictures += decoder.decode(unit).size();
    QVERIFY(pictures > 0);
}

void TstStream::decoderWaitsForKeyFrameAfterReset()
{
    if (!H264Decoder::isAvailable())
        QSKIP("built without FFmpeg");
    H264Decoder decoder;
    const QList<QByteArray> units = accessUnits(sample());
    QVERIFY(units.size() > 8);
    for (int i = 0; i < 3; ++i)
        decoder.decode(units[i]);
    decoder.reset();

    // Delta frames alone must not produce garbage pictures...
    QCOMPARE(decoder.decode(units[3]).size(), 0);
    // ...but a key frame (units[5] starts the second GOP with SPS/PPS/IDR) brings the picture back.
    int pictures = 0;
    for (int i = 5; i < units.size(); ++i)
        pictures += decoder.decode(units[i]).size();
    QVERIFY(pictures > 0);
}

QTEST_GUILESS_MAIN(TstStream)
#include "tst_stream.moc"
