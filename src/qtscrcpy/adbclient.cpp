#include "adbclient.h"

#include "commandrunner.h"

#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QHostAddress>
#include <QTcpSocket>

namespace QtScrcpy {

namespace {

constexpr int kSyncChunk = 64 * 1024;

void putU32le(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xff)).append(char((v >> 8) & 0xff)).append(char((v >> 16) & 0xff)).append(char((v >> 24) & 0xff));
}

quint32 getU32le(const char *p)
{
    const auto *u = reinterpret_cast<const quint8 *>(p);
    return quint32(u[0]) | (quint32(u[1]) << 8) | (quint32(u[2]) << 16) | (quint32(u[3]) << 24);
}

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

} // namespace

AdbClient::AdbClient(const QString &adbExecutable, int port)
    : m_adb(adbExecutable)
{
    if (port > 0) {
        m_port = port;
    } else {
        bool ok = false;
        const int fromEnv = qEnvironmentVariable("ANDROID_ADB_SERVER_PORT").toInt(&ok);
        m_port = ok && fromEnv > 0 && fromEnv < 65536 ? fromEnv : 5037;
    }
}

bool AdbClient::readExact(QTcpSocket &socket, char *buffer, int size, int timeoutMs)
{
    int have = 0;
    while (have < size) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(timeoutMs))
            return false;
        const qint64 n = socket.read(buffer + have, size - have);
        if (n < 0)
            return false;
        have += int(n);
    }
    return true;
}

std::unique_ptr<QTcpSocket> AdbClient::connectToServer(QString *error)
{
    auto socket = std::make_unique<QTcpSocket>();
    socket->connectToHost(QHostAddress::LocalHost, quint16(m_port));
    if (!socket->waitForConnected(kTimeoutMs)) {
        setError(error, QStringLiteral("cannot reach the adb server on port %1: %2").arg(m_port).arg(socket->errorString()));
        return nullptr;
    }
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    return socket;
}

bool AdbClient::ensureServer(QString *error)
{
    QString firstError;
    if (connectToServer(&firstError))
        return true;

    if (m_adb.isEmpty()) {
        setError(error, firstError + QStringLiteral(" (and no adb executable to start it)"));
        return false;
    }
    const CommandResult started = CommandRunner::runSync(m_adb, {QStringLiteral("start-server")}, 30 * 1000);
    if (!started.ok()) {
        setError(error, QStringLiteral("cannot start the adb server: %1").arg(started.failureReason()));
        return false;
    }
    return connectToServer(error) != nullptr;
}

bool AdbClient::sendRequest(QTcpSocket &socket, const QString &request, QString *error)
{
    const QByteArray payload = request.toUtf8();
    if (payload.size() > 0xffff) {
        setError(error, QStringLiteral("adb request too long"));
        return false;
    }
    const QByteArray frame = QByteArray::number(payload.size(), 16).rightJustified(4, '0') + payload;
    if (socket.write(frame) != frame.size() || !socket.waitForBytesWritten(kTimeoutMs)) {
        setError(error, QStringLiteral("cannot talk to the adb server: %1").arg(socket.errorString()));
        return false;
    }
    return true;
}

bool AdbClient::readStatus(QTcpSocket &socket, QString *error)
{
    char status[4];
    if (!readExact(socket, status, 4, kTimeoutMs)) {
        setError(error, QStringLiteral("the adb server did not answer"));
        return false;
    }
    if (memcmp(status, "OKAY", 4) == 0)
        return true;
    if (memcmp(status, "FAIL", 4) == 0) {
        char len[4];
        QString message = QStringLiteral("adb request failed");
        if (readExact(socket, len, 4, kTimeoutMs)) {
            bool ok = false;
            const int n = QByteArray(len, 4).toInt(&ok, 16);
            if (ok && n > 0 && n < 4096) {
                QByteArray text(n, '\0');
                if (readExact(socket, text.data(), n, kTimeoutMs))
                    message = QString::fromUtf8(text);
            }
        }
        setError(error, message);
        return false;
    }
    setError(error, QStringLiteral("unexpected answer from the adb server"));
    return false;
}

bool AdbClient::hostQuery(const QString &request, QString *answer, QString *error)
{
    auto socket = connectToServer(error);
    if (!socket || !sendRequest(*socket, request, error) || !readStatus(*socket, error))
        return false;
    char len[4];
    if (!readExact(*socket, len, 4, kTimeoutMs)) {
        setError(error, QStringLiteral("truncated answer from the adb server"));
        return false;
    }
    bool ok = false;
    const int n = QByteArray(len, 4).toInt(&ok, 16);
    if (!ok || n < 0 || n > 1024 * 1024) {
        setError(error, QStringLiteral("bad answer length from the adb server"));
        return false;
    }
    QByteArray text(n, '\0');
    if (n > 0 && !readExact(*socket, text.data(), n, kTimeoutMs)) {
        setError(error, QStringLiteral("truncated answer from the adb server"));
        return false;
    }
    if (answer)
        *answer = QString::fromUtf8(text);
    return true;
}

bool AdbClient::forward(const QString &serial, int localPort, const QString &remote, QString *error)
{
    auto socket = connectToServer(error);
    const QString request = QStringLiteral("host-serial:%1:forward:tcp:%2;%3").arg(serial).arg(localPort).arg(remote);
    if (!socket || !sendRequest(*socket, request, error) || !readStatus(*socket, error))
        return false;
    // Some servers acknowledge twice (request accepted, then forward established).
    if (socket->waitForReadyRead(300) && socket->bytesAvailable() >= 4) {
        QString second;
        if (!readStatus(*socket, &second)) {
            setError(error, second);
            return false;
        }
    }
    return true;
}

bool AdbClient::removeForward(const QString &serial, int localPort, QString *error)
{
    auto socket = connectToServer(error);
    const QString request = QStringLiteral("host-serial:%1:killforward:tcp:%2").arg(serial).arg(localPort);
    return socket && sendRequest(*socket, request, error) && readStatus(*socket, error);
}

std::unique_ptr<QTcpSocket> AdbClient::openService(const QString &serial, const QString &service, QString *error)
{
    auto socket = connectToServer(error);
    if (!socket)
        return nullptr;
    if (!sendRequest(*socket, QStringLiteral("host:transport:%1").arg(serial), error) || !readStatus(*socket, error))
        return nullptr;
    if (!sendRequest(*socket, service, error) || !readStatus(*socket, error))
        return nullptr;
    return socket;
}

std::unique_ptr<QTcpSocket> AdbClient::shell(const QString &serial, const QString &command, QString *error)
{
    return openService(serial, QStringLiteral("shell:") + command, error);
}

bool AdbClient::push(const QString &serial, const QString &localFile, const QString &remotePath, QString *error, int mode)
{
    QFile file(localFile);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("cannot read %1: %2").arg(localFile, file.errorString()));
        return false;
    }
    auto socket = openService(serial, QStringLiteral("sync:"), error);
    if (!socket)
        return false;

    const QByteArray target = (remotePath + QLatin1Char(',') + QString::number(mode)).toUtf8();
    QByteArray out("SEND", 4);
    putU32le(out, quint32(target.size()));
    out += target;
    if (socket->write(out) != out.size()) {
        setError(error, QStringLiteral("push failed: %1").arg(socket->errorString()));
        return false;
    }

    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kSyncChunk);
        if (chunk.isEmpty() && !file.atEnd()) {
            setError(error, QStringLiteral("cannot read %1").arg(localFile));
            return false;
        }
        QByteArray packet("DATA", 4);
        putU32le(packet, quint32(chunk.size()));
        packet += chunk;
        if (socket->write(packet) != packet.size() || !socket->waitForBytesWritten(kTimeoutMs)) {
            setError(error, QStringLiteral("push failed: %1").arg(socket->errorString()));
            return false;
        }
    }

    QByteArray done("DONE", 4);
    putU32le(done, quint32(QFileInfo(localFile).lastModified().toSecsSinceEpoch()));
    socket->write(done);
    socket->waitForBytesWritten(kTimeoutMs);

    char reply[8];
    if (!readExact(*socket, reply, 8, 30 * 1000)) {
        setError(error, QStringLiteral("the device did not confirm the file"));
        return false;
    }
    if (memcmp(reply, "OKAY", 4) != 0) {
        QString message = QStringLiteral("push was refused by the device");
        const quint32 n = getU32le(reply + 4);
        if (memcmp(reply, "FAIL", 4) == 0 && n > 0 && n < 4096) {
            QByteArray text(int(n), '\0');
            if (readExact(*socket, text.data(), int(n), kTimeoutMs))
                message = QStringLiteral("push failed: %1").arg(QString::fromUtf8(text));
        }
        setError(error, message);
        return false;
    }

    QByteArray quit("QUIT", 4);
    putU32le(quit, 0);
    socket->write(quit);
    socket->waitForBytesWritten(kTimeoutMs);
    return true;
}

} // namespace QtScrcpy
