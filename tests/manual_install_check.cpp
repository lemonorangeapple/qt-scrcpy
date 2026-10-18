// Drives the automatic HarmonyOS install path against a fake hdc and a fake unsigned HAP:
#include <qtscrcpy/hdc.h>
// the session must read the UDID, sign the package for it and ask hdc to install it.
#include <qtscrcpy/mirror.h>

#include <QCoreApplication>
#include <QTimer>
#include <QtGlobal>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qputenv("HDC", qgetenv("FAKE_HDC"));
    qputenv("QTSCRCPY_HAP", qgetenv("FAKE_HAP"));
    qputenv("QTSCRCPY_SIGNING_DIR", qgetenv("SIGNING_DIR"));
    qputenv("JAVA_HOME", qgetenv("JAVA_HOME"));

    QtScrcpy::Hdc hdc;
    QtScrcpy::MirrorOptions options;
    options.backend = QtScrcpy::Backend::Hdc;
    options.serial = QStringLiteral("FAKEUDID0001");
    options.source = QtScrcpy::MirrorSource::Screen;

    QtScrcpy::MirrorSession session(options, &hdc);
    QString outcome;
    QObject::connect(&session, &QtScrcpy::MirrorSession::finished, [&](const QString &error) {
        outcome = error.isEmpty() ? QStringLiteral("(stopped)") : error;
        app.quit();
    });
    QTimer::singleShot(100, &session, &QtScrcpy::MirrorSession::start);
    QTimer::singleShot(180000, &app, [&] {
        outcome = QStringLiteral("TIMEOUT");
        app.quit();
    });
    app.exec();
    std::printf("outcome: %s\n", qPrintable(outcome));
    return 0;
}
