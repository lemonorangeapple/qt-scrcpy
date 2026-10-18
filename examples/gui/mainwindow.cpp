#include "mainwindow.h"
#include "videowindow.h"
#include <qtscrcpy/scrcpystream.h>

#include <QComboBox>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

using namespace QtScrcpy;

namespace {

constexpr int kSerialRole = Qt::UserRole;
constexpr int kHdcRole = Qt::UserRole + 1;

QString stateHint(const AdbDevice &device)
{
    switch (device.state) {
    case AdbDevice::State::Device:
        return {};
    case AdbDevice::State::Unauthorized:
        return QObject::tr("等待授权：请在手机上点击“允许”");
    case AdbDevice::State::Offline:
        return QObject::tr("离线，请重新插拔数据线");
    case AdbDevice::State::NoPermissions:
        return QObject::tr("没有访问权限（Linux 请检查 udev 规则）");
    case AdbDevice::State::Authorizing:
    case AdbDevice::State::Connecting:
        return QObject::tr("正在连接…");
    default:
        return device.stateText;
    }
}

QString sessionHint(const MirrorSession *session)
{
    if (!session)
        return {};
    switch (session->state()) {
    case MirrorSession::State::Starting:
        return QObject::tr("正在启动投屏…");
    case MirrorSession::State::Streaming:
        return QObject::tr("投屏中");
    case MirrorSession::State::Stopped:
        break;
    }
    return {};
}

QString describe(const AdbDevice &device, bool hdc, const MirrorSession *session)
{
    QString text = device.displayName() == device.serial
                       ? device.serial
                       : QStringLiteral("%1\n%2").arg(device.displayName(), device.serial);
    QString hint = sessionHint(session);
    if (hint.isEmpty())
        hint = stateHint(device);
    if (hdc)
        hint = hint.isEmpty() ? QStringLiteral("hdc") : QStringLiteral("hdc · %1").arg(hint);
    if (!hint.isEmpty())
        text += QStringLiteral("\n%1").arg(hint);
    return text;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("qt-scrcpy 传屏"));
    resize(400, 520);
    buildUi();

    connect(&m_adb, &Adb::devicesChanged, this, &MainWindow::rebuildDeviceList);
    connect(&m_hdc, &Hdc::targetsChanged, this, &MainWindow::rebuildDeviceList);
    connect(&m_adb, &Adb::devicesChanged, this, [this](const QList<AdbDevice> &devices) {
        if (!devices.isEmpty())
            m_lastAdbProblem.clear();
    });
    // adb problems (a slow first start, a device that went away) are reported once, in the status bar,
    // and never as a dialog: they usually fix themselves on the next poll.
    connect(&m_adb, &Adb::errorOccurred, this, [this](const QString &message) {
        if (!m_adb.isAvailable())
            return; // a missing adb is explained by the warning shown at start-up
        m_lastAdbProblem = message;
        const QString text = tr("adb：%1").arg(message);
        statusBar()->showMessage(text, 15000);
        if (!m_reported.contains(text)) {
            m_reported.insert(text);
            reportError(text);
        }
    });
    connect(&m_monitor, &DeviceMonitor::deviceDetached, this, [this](const AdbDevice &device) {
        if (MirrorSession *session = m_sessions.value(keyOf({device, Backend::Adb})))
            session->stop();
    });

    QString problem;
    if (!m_adb.isAvailable() && !m_hdc.isAvailable()) {
        problem = tr("未找到 adb 或 hdc。\n"
                     "安卓设备需要 Android platform-tools 里的 adb，鸿蒙设备需要 hdc；"
                     "也可以设置环境变量 ADB / HDC。");
    } else if (!H264Decoder::isAvailable()) {
        problem = tr("这个版本没有带视频解码库（FFmpeg），无法显示画面。");
    }
    if (!problem.isEmpty())
        QTimer::singleShot(0, this, [this, problem] { QMessageBox::warning(this, windowTitle(), problem); });

    updateUi();
    if (m_adb.isAvailable())
        m_monitor.start();
    if (m_hdc.isAvailable())
        m_hdc.startPolling();
}

MainWindow::~MainWindow()
{
    for (MirrorSession *session : m_sessions)
        session->stop();
    for (const QPointer<VideoWindow> &window : m_windows) {
        if (window)
            window->close();
    }
}

void MainWindow::buildUi()
{
    m_deviceList = new QListWidget;
    m_deviceList->setToolTip(tr("用数据线连接手机，并在手机上允许 USB 调试"));
    m_source = new QComboBox;
    m_source->addItem(tr("屏幕"), int(MirrorSource::Screen));
    m_source->addItem(tr("后置摄像头"), int(MirrorSource::BackCamera));
    m_source->addItem(tr("前置摄像头"), int(MirrorSource::FrontCamera));
    m_source->addItem(tr("外接摄像头"), int(MirrorSource::ExternalCamera));
    m_source->setToolTip(tr("摄像头投屏：安卓需要 Android 12+；鸿蒙(hdc)设备需要先安装配套应用，且没有外接摄像头"));
    m_toggleButton = new QPushButton(tr("开始投屏"));
    m_toggleButton->setMinimumHeight(36);
    m_toggleButton->setDefault(true);
    m_toggleButton->setEnabled(false);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->addWidget(m_deviceList, 1);
    m_refreshButton = new QPushButton(tr("刷新"));
    m_refreshButton->setToolTip(tr("重新读取设备列表（插拔设备后如果列表没更新，点这里）"));

    auto *row = new QHBoxLayout;
    row->addWidget(m_source);
    row->addWidget(m_toggleButton, 1);
    row->addWidget(m_refreshButton);
    layout->addLayout(row);
    setCentralWidget(central);

    connect(m_deviceList, &QListWidget::currentRowChanged, this, &MainWindow::updateUi);
    connect(m_toggleButton, &QPushButton::clicked, this, &MainWindow::toggleMirroring);
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshDevices);
}

QString MainWindow::keyOf(const Entry &entry)
{
    return (entry.backend == Backend::Hdc ? QStringLiteral("hdc:") : QStringLiteral("adb:")) + entry.device.serial;
}

MainWindow::Entry MainWindow::selectedEntry() const
{
    const QListWidgetItem *item = m_deviceList->currentItem();
    if (!item)
        return {};
    Entry entry;
    entry.backend = item->data(kHdcRole).toBool() ? Backend::Hdc : Backend::Adb;
    const QString serial = item->data(kSerialRole).toString();
    if (entry.backend == Backend::Adb) {
        entry.device = m_monitor.device(serial);
    } else {
        for (const AdbDevice &target : m_hdc.targets()) {
            if (target.serial == serial)
                entry.device = target;
        }
    }
    return entry;
}

bool MainWindow::isMirroring(const QString &key) const
{
    const MirrorSession *session = m_sessions.value(key);
    return session && session->state() != MirrorSession::State::Stopped;
}

MirrorSource MainWindow::currentSource() const
{
    return static_cast<MirrorSource>(m_source->currentData().toInt());
}

void MainWindow::rebuildDeviceList()
{
    const QListWidgetItem *current = m_deviceList->currentItem();
    const QString previous = current ? current->data(kSerialRole).toString() : QString();
    const bool previousHdc = current && current->data(kHdcRole).toBool();

    int previousRow = -1;
    int firstOnlineRow = -1;
    {
        const QSignalBlocker blocker(m_deviceList);
        m_deviceList->clear();
        const auto add = [&](const AdbDevice &device, bool hdc) {
            const int row = m_deviceList->count();
            const Entry entry{device, hdc ? Backend::Hdc : Backend::Adb};
            auto *item = new QListWidgetItem(describe(device, hdc, m_sessions.value(keyOf(entry))), m_deviceList);
            item->setData(kSerialRole, device.serial);
            item->setData(kHdcRole, hdc);
            if (!device.isOnline())
                item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
            else if (firstOnlineRow < 0)
                firstOnlineRow = row;
            if (device.serial == previous && hdc == previousHdc)
                previousRow = row;
        };
        for (const AdbDevice &device : m_monitor.devices())
            add(device, false);
        for (const AdbDevice &device : m_hdc.targets())
            add(device, true);
    }

    const int row = previousRow >= 0 ? previousRow : firstOnlineRow;
    if (row >= 0)
        m_deviceList->setCurrentRow(row);
    statusBar()->showMessage(toolingSummary());
    updateUi();
}

void MainWindow::updateUi()
{
    const Entry entry = selectedEntry();
    const bool hdc = entry.backend == Backend::Hdc;
    const bool hasSelection = !entry.device.serial.isEmpty();
    const bool busy = hasSelection && isMirroring(keyOf(entry));

    m_source->setEnabled(!busy);
    if (auto *model = qobject_cast<QStandardItemModel *>(m_source->model())) {
        if (QStandardItem *external = model->item(3))
            external->setEnabled(!hdc); // the HarmonyOS app offers back and front cameras only
    }
    if (hdc && currentSource() == MirrorSource::ExternalCamera)
        m_source->setCurrentIndex(0);

    if (busy) {
        m_toggleButton->setText(tr("停止投屏"));
        m_toggleButton->setEnabled(true);
    } else {
        m_toggleButton->setText(tr("开始投屏"));
        // Clickable as long as something is selected: pressing it explains what is missing instead of
        // looking like nothing happened.
        m_toggleButton->setEnabled(hasSelection);
    }
}

QString MainWindow::toolingSummary() const
{
    QStringList parts;
    parts << (m_adb.isAvailable()
                  ? tr("adb %1").arg(QFileInfo(m_adb.executable()).fileName())
                  : tr("adb：未找到"));
    parts << (m_hdc.isAvailable() ? tr("hdc：已找到") : tr("hdc：未找到"));
    const QString jar = ScrcpyStream::findServerJar();
    parts << (jar.isEmpty() ? tr("scrcpy-server：未找到") : tr("scrcpy-server：已找到"));
    parts << tr("设备 %1 台").arg(m_deviceList->count());
    return parts.join(QStringLiteral(" · "));
}

void MainWindow::refreshDevices()
{
    // Both tools are asked again; the HarmonyOS one polls by itself, the adb one only on request.
    if (m_adb.isAvailable()) {
        m_adb.refreshDevices();
        if (!m_monitor.devices().isEmpty())
            m_monitor.start(); // also (re)starts the tracker when it is not running
    }
    if (m_hdc.isAvailable())
        m_hdc.refreshTargets();
    statusBar()->showMessage(tr("已请求刷新 · %1").arg(toolingSummary()), 8000);

    // Nothing found: say why, because an empty list and a list that is merely slow look the same.
    QTimer::singleShot(1500, this, [this] {
        if (m_deviceList->count() > 0) {
            statusBar()->showMessage(toolingSummary());
            return;
        }
        QStringList lines;
        lines << toolingSummary();
        if (!m_lastAdbProblem.isEmpty())
            lines << tr("最近一次 adb 报错：%1").arg(m_lastAdbProblem);
        lines << QString();
        lines << tr("请检查：");
        lines << tr("• 手机上是否打开「开发者选项 → USB 调试」，插上后是否点了「允许」");
        lines << tr("• 数据线是否支持数据传输（有些只能充电）");
        lines << tr("• 是否安装了多个版本的 adb（DevEco、其他工具自带），它们会互相抢服务端");
        lines << tr("• 安卓设备还需要 scrcpy-server，鸿蒙设备还需要 hdc（见 README）");
        reportError(lines.join(QLatin1Char('\n')));
    });
}

void MainWindow::toggleMirroring()
{
    const Entry entry = selectedEntry();
    if (entry.device.serial.isEmpty())
        return;
    if (MirrorSession *session = m_sessions.value(keyOf(entry)); session && isMirroring(keyOf(entry))) {
        session->stop();
        return;
    }
    startMirroring(entry);
}

void MainWindow::startMirroring(const Entry &entry)
{
    // Every reason a click would otherwise do nothing, said out loud.
    QStringList reasons;
    if (!H264Decoder::isAvailable())
        reasons << tr("这个版本没有带视频解码库（FFmpeg），无法显示画面。");
    if (entry.backend == Backend::Hdc) {
        if (!m_hdc.isAvailable())
            reasons << tr("未找到 hdc：鸿蒙设备需要它（DevEco Studio 的 SDK 里有）。");
    } else {
        if (!m_adb.isAvailable())
            reasons << tr("未找到 adb：安卓设备需要 platform-tools。");
        if (ScrcpyStream::findServerJar().isEmpty())
            reasons << tr("未找到 scrcpy-server：请把随包的 scrcpy-server 放在程序目录，或设置 SCRCPY_SERVER_PATH。");
    }
    if (entry.device.state != AdbDevice::State::Device)
        reasons << tr("设备当前状态：%1（请在手机上允许 USB 调试后重试）").arg(entry.device.stateText);
    if (!reasons.isEmpty()) {
        reportError(tr("现在还不能投屏这个设备：\n\n%1").arg(reasons.join(QLatin1Char('\n'))));
        return;
    }
    const QString key = keyOf(entry);
    if (MirrorSession *old = m_sessions.take(key))
        old->deleteLater();

    MirrorOptions options;
    options.backend = entry.backend;
    options.serial = entry.device.serial;
    options.source = currentSource();
    options.maxSize = 1920;

    auto *session = new MirrorSession(options, &m_hdc, m_adb.executable(), this);
    m_sessions.insert(key, session);

    const QString title = QStringLiteral("%1 - %2").arg(entry.device.displayName(), m_source->currentText());
    auto *window = new VideoWindow(title);
    m_windows.insert(key, window);
    connect(session, &MirrorSession::frameReady, window, &VideoWindow::showFrame);
    connect(window, &VideoWindow::closed, session, &MirrorSession::stop);
    connect(session, &MirrorSession::stateChanged, this, &MainWindow::rebuildDeviceList);
    connect(session, &MirrorSession::notice, this, [this, window](const QString &text) {
        // The reason matters (a slow picture is otherwise baffling), so it goes to the status bar and the
        // window title as well as being shown once in a dialog.
        statusBar()->showMessage(text, 20000);
        window->setWindowTitle(window->windowTitle() + tr(" [低帧率]"));
        QMessageBox::information(window, windowTitle(), text);
    });
    connect(session, &MirrorSession::installRequested, this,
            [this, window](const QString &serial, std::function<void(bool)> answer) {
        const auto choice = QMessageBox::question(
            window, windowTitle(),
            tr("设备 %1 上还没有配套应用。\n\n是否现在安装？会读取手机的 UDID 为应用签名后安装。\n选“否”则改用系统截图投屏（帧率很低，只有屏幕）。").arg(serial),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        answer(choice == QMessageBox::Yes);
    });
    connect(session, &MirrorSession::finished, this, [this, key, serial = entry.device.serial](const QString &error) {
        if (const QPointer<VideoWindow> w = m_windows.take(key))
            w->close();
        if (!error.isEmpty())
            reportError(tr("设备 %1 投屏失败：%2").arg(serial, error));
        rebuildDeviceList();
    });
    window->show();
    session->start();
    rebuildDeviceList();
}

void MainWindow::reportError(const QString &message)
{
    if (!message.isEmpty())
        QMessageBox::warning(this, windowTitle(), message);
}
