#pragma once

#include "qtscrcpy/qtscrcpy_export.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <cstdint>

namespace QtScrcpy {

// Wire format spoken by the HarmonyOS streamer app (see harmony/PROTOCOL.md).
namespace StreamProtocol {

constexpr int kDefaultDevicePort = 27183;
constexpr int kHeaderSize = 16;
constexpr int kPacketHeaderSize = 16;
constexpr quint32 kMaxPayload = 8u * 1024 * 1024;

enum class Source : quint8 { Screen = 0, BackCamera = 1, FrontCamera = 2 };
enum class PacketType : quint8 { Config = 0, KeyFrame = 1, Frame = 2 };

struct QTSCRCPY_EXPORT Header
{
    Source source = Source::Screen;
    quint8 codec = 0;
    quint32 width = 0;
    quint32 height = 0;
};

struct QTSCRCPY_EXPORT Packet
{
    PacketType type = PacketType::Frame;
    quint64 timestampUs = 0;
    QByteArray payload;
};

// Builders, used by the tests and by anything that has to play the device's role.
QTSCRCPY_EXPORT QByteArray encodeHeader(const Header &header);
QTSCRCPY_EXPORT QByteArray encodePacket(const Packet &packet);

// Incremental parser: feed it whatever bytes arrive from the socket.
class QTSCRCPY_EXPORT Parser
{
public:
    enum class Status { NeedMore, Header, Packet, Error };

    void append(const QByteArray &data) { m_buffer += data; }

    // Extracts the next item. Header comes exactly once, first.
    Status next(Header *header, Packet *packet);

    QString errorString() const { return m_error; }
    bool hasHeader() const { return m_gotHeader; }

private:
    QByteArray m_buffer;
    bool m_gotHeader = false;
    QString m_error;
};

} // namespace StreamProtocol
} // namespace QtScrcpy
