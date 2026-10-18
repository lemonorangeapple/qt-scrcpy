#include "mirror.h"

#include "hdcstream.h"
#include "scrcpystream.h"

#include <QMetaMethod>

namespace QtScrcpy {

class MirrorSession::Impl
{
public:
    ScrcpyStream *adb = nullptr;
    HdcStream *hdc = nullptr;
};

MirrorSession::MirrorSession(const MirrorOptions &options, Hdc *hdc, const QString &adbExecutable, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , d(new Impl)
{
    qRegisterMetaType<VideoFrame>("QtScrcpy::VideoFrame");
    qRegisterMetaType<State>("QtScrcpy::MirrorSession::State");

    if (!unsupportedReason(options.backend, options.source).isEmpty())
        return; // start() reports it

    if (options.backend == Backend::Adb) {
        ScrcpyStream::Options o;
        o.serial = options.serial;
        switch (options.source) {
        case MirrorSource::BackCamera:
            o.camera = ScrcpyStream::Camera::Back;
            break;
        case MirrorSource::FrontCamera:
            o.camera = ScrcpyStream::Camera::Front;
            break;
        case MirrorSource::ExternalCamera:
            o.camera = ScrcpyStream::Camera::External;
            break;
        case MirrorSource::Screen:
            break;
        }
        o.maxSize = options.maxSize;
        o.bitRate = options.bitRate;
        o.maxFps = options.maxFps;
        d->adb = new ScrcpyStream(o, adbExecutable, {}, this);
        connect(d->adb, &ScrcpyStream::frameReady, this, &MirrorSession::frameReady);
        connect(d->adb, &ScrcpyStream::stateChanged, this, [this](ScrcpyStream::State s) {
            setState(s == ScrcpyStream::State::Streaming ? State::Streaming
                     : s == ScrcpyStream::State::Starting ? State::Starting : State::Stopped);
        });
        connect(d->adb, &ScrcpyStream::finished, this, [this](const QString &error) {
            m_finishedEmitted = true;
            setState(State::Stopped);
            emit finished(error);
        });
    } else if (hdc) {
        StreamProtocol::Source source = StreamProtocol::Source::Screen;
        if (options.source == MirrorSource::BackCamera)
            source = StreamProtocol::Source::BackCamera;
        else if (options.source == MirrorSource::FrontCamera)
            source = StreamProtocol::Source::FrontCamera;
        d->hdc = new HdcStream(hdc, options.serial, source, this);
        if (!options.hapToInstall.isEmpty())
            d->hdc->setHapToInstall(options.hapToInstall, options.hapIsUnsigned);
        connect(d->hdc, &HdcStream::frameReady, this, &MirrorSession::frameReady);
        connect(d->hdc, &HdcStream::notice, this, &MirrorSession::notice);
        connect(d->hdc, &HdcStream::stateChanged, this, [this](HdcStream::State s) {
            setState(s == HdcStream::State::Streaming ? State::Streaming
                     : s == HdcStream::State::Stopped ? State::Stopped : State::Starting);
        });
        connect(d->hdc, &HdcStream::finished, this, [this](const QString &error) {
            m_finishedEmitted = true;
            setState(State::Stopped);
            emit finished(error);
        });
    }
}

MirrorSession::~MirrorSession() = default;

QString MirrorSession::unsupportedReason(Backend backend, MirrorSource source)
{
    if (backend == Backend::Hdc && source == MirrorSource::ExternalCamera)
        return tr("鸿蒙(hdc)设备只支持屏幕、后置和前置摄像头。");
    return {};
}

void MirrorSession::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}

void MirrorSession::start()
{
    if (m_state != State::Stopped)
        return;
    m_finishedEmitted = false;

    const QString reason = unsupportedReason(m_options.backend, m_options.source);
    if (!reason.isEmpty() || (!d->adb && !d->hdc)) {
        m_finishedEmitted = true;
        emit finished(reason.isEmpty() ? tr("hdc 会话需要一个 Hdc 对象。") : reason);
        return;
    }
    if (d->hdc && isSignalConnected(QMetaMethod::fromSignal(&MirrorSession::installRequested))) {
        d->hdc->setInstallConfirmation([this](const QString &serial, std::function<void(bool)> answer) {
            emit installRequested(serial, std::move(answer));
        });
    }
    setState(State::Starting);
    if (d->adb)
        d->adb->start();
    else
        d->hdc->start();
}

void MirrorSession::stop()
{
    if (d->adb)
        d->adb->stop();
    else if (d->hdc)
        d->hdc->stop();
}

} // namespace QtScrcpy
