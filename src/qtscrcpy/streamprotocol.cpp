#include "streamprotocol.h"

namespace QtScrcpy {
namespace StreamProtocol {

namespace {

const char kMagic[4] = {'Q', 'S', 'C', '1'};

void putU32(QByteArray &out, quint32 value)
{
    out.append(char(value >> 24)).append(char(value >> 16)).append(char(value >> 8)).append(char(value));
}

quint32 getU32(const char *p)
{
    const auto *u = reinterpret_cast<const quint8 *>(p);
    return (quint32(u[0]) << 24) | (quint32(u[1]) << 16) | (quint32(u[2]) << 8) | quint32(u[3]);
}

} // namespace

QByteArray encodeHeader(const Header &header)
{
    QByteArray out(kMagic, 4);
    out.append(char(header.source)).append(char(header.codec)).append(char(0)).append(char(0));
    putU32(out, header.width);
    putU32(out, header.height);
    return out;
}

QByteArray encodePacket(const Packet &packet)
{
    QByteArray out;
    out.append(char(packet.type)).append(char(0)).append(char(0)).append(char(0));
    putU32(out, quint32(packet.timestampUs >> 32));
    putU32(out, quint32(packet.timestampUs & 0xffffffffu));
    putU32(out, quint32(packet.payload.size()));
    out += packet.payload;
    return out;
}

Parser::Status Parser::next(Header *header, Packet *packet)
{
    if (!m_error.isEmpty())
        return Status::Error;

    if (!m_gotHeader) {
        if (m_buffer.size() < kHeaderSize)
            return Status::NeedMore;
        if (memcmp(m_buffer.constData(), kMagic, 4) != 0) {
            m_error = QStringLiteral("not a qt-scrcpy stream (bad magic)");
            return Status::Error;
        }
        const char *p = m_buffer.constData();
        if (quint8(p[4]) > quint8(Source::FrontCamera)) {
            m_error = QStringLiteral("unknown video source %1").arg(int(quint8(p[4])));
            return Status::Error;
        }
        if (p[5] > 2) {
            m_error = QStringLiteral("unsupported codec %1").arg(int(quint8(p[5])));
            return Status::Error;
        }
        if (header) {
            header->source = Source(quint8(p[4]));
            header->codec = quint8(p[5]);
            header->width = getU32(p + 8);
            header->height = getU32(p + 12);
        }
        m_buffer.remove(0, kHeaderSize);
        m_gotHeader = true;
        return Status::Header;
    }

    if (m_buffer.size() < kPacketHeaderSize)
        return Status::NeedMore;
    const char *p = m_buffer.constData();
    if (quint8(p[0]) > quint8(PacketType::Frame)) {
        m_error = QStringLiteral("unknown packet type %1").arg(int(quint8(p[0])));
        return Status::Error;
    }
    const quint32 size = getU32(p + 12);
    if (size > kMaxPayload) {
        m_error = QStringLiteral("packet of %1 bytes is too large").arg(size);
        return Status::Error;
    }
    if (quint32(m_buffer.size()) < quint32(kPacketHeaderSize) + size)
        return Status::NeedMore;

    if (packet) {
        packet->type = PacketType(quint8(p[0]));
        packet->timestampUs = (quint64(getU32(p + 4)) << 32) | getU32(p + 8);
        packet->payload = m_buffer.mid(kPacketHeaderSize, int(size));
    }
    m_buffer.remove(0, kPacketHeaderSize + int(size));
    return Status::Packet;
}

} // namespace StreamProtocol
} // namespace QtScrcpy
