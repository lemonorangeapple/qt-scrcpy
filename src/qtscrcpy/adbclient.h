#pragma once

#include "qtscrcpy/qtscrcpy_export.h"

#include <QString>

#include <memory>

class QTcpSocket;

namespace QtScrcpy {

// Speaks the adb "smart socket" protocol to the adb server (default 127.0.0.1:5037) directly, so a
// mirroring session does not need to start an `adb` process for every step. The server itself is
// still `adb.exe`: ensureServer() starts it when it is not running.
//
// All calls block (with timeouts) and may be used from any thread, one AdbClient per thread.
class QTSCRCPY_EXPORT AdbClient
{
public:
    // `adbExecutable` is only used to start the server. `port` 0 = $ANDROID_ADB_SERVER_PORT or 5037.
    explicit AdbClient(const QString &adbExecutable = {}, int port = 0);

    int port() const { return m_port; }

    // True when a server answers; starts one with `adb start-server` if none does.
    bool ensureServer(QString *error);

    // `host:...` request that returns a length-prefixed answer (e.g. "host:version").
    bool hostQuery(const QString &request, QString *answer, QString *error);

    // `adb -s serial forward tcp:<localPort> <remote>`; remote e.g. "localabstract:scrcpy_1234abcd".
    bool forward(const QString &serial, int localPort, const QString &remote, QString *error);
    bool removeForward(const QString &serial, int localPort, QString *error = nullptr);

    // Copies a local file to the device (sync protocol).
    bool push(const QString &serial, const QString &localFile, const QString &remotePath, QString *error,
              int mode = 0100644);

    // Opens `shell:<command>`: the returned socket yields the command's output until it exits;
    // closing the socket ends the command. Null on failure.
    std::unique_ptr<QTcpSocket> shell(const QString &serial, const QString &command, QString *error);

    // Opens a service on a device (after `host:transport:<serial>`), e.g. "sync:" or "shell:ls".
    std::unique_ptr<QTcpSocket> openService(const QString &serial, const QString &service, QString *error);

    static constexpr int kTimeoutMs = 8000;

private:
    std::unique_ptr<QTcpSocket> connectToServer(QString *error);
    bool sendRequest(QTcpSocket &socket, const QString &request, QString *error);
    bool readStatus(QTcpSocket &socket, QString *error);
    static bool readExact(QTcpSocket &socket, char *buffer, int size, int timeoutMs);

    QString m_adb;
    int m_port;
};

} // namespace QtScrcpy
