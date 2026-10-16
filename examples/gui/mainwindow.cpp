#include "mainwindow.h"

#include "modedialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

using namespace QtScrcpy;

namespace {
constexpr int kSerialRole = Qt::UserRole;

QString stateHint(const AdbDevice &device)
{
    switch (device.state) {
    case AdbDevice::State::Device:
        return {};
    case AdbDevice::State::Unauthorized:
        return QObject::tr("等待授权:请在手机上点击“允许”");
    case AdbDevice::State::Offline:
        return QObject::tr("离线,请重新插拔数据线");
    case AdbDevice::State::NoPermissions:
        return QObject::tr("没有访问权限(Linux 请检查 udev 规则)");
    case AdbDevice::State::Authorizing:
    case AdbDevice::State::Connecting:
        return QObject::tr("正在连接…");
    default:
        return device.stateText;
    }
}

QString describe(const AdbDevice &device)
{
    // The model is unknown until the phone has authorized this computer; then the name is
    // just the serial and showing it twice would be noise.
    QString text = device.displayName() == device.serial
                       ? device.serial
                       : QStringLiteral("%1\n%2").arg(device.displayName(), device.serial);
    const QString hint = stateHint(device);
    if (!hint.isEmpty())
        text += QStringLiteral("  — %1").arg(hint);
    return text;
}

void styleBanner(QLabel *label, const QString &background, const QString &foreground)
{
    label->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border-radius: 6px; padding: 8px; }")
                             .arg(background, foreground));
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("qt-scrcpy 投屏"));
    resize(980, 720);
    buildUi();

    connect(&m_adb, &Adb::devicesChanged, this, &MainWindow::rebuildDeviceList);
    connect(&m_adb, &Adb::errorOccurred, this, [this](const QString &message) {
        log(tr("adb: %1").arg(message));
        updateStatus();
    });
    connect(&m_monitor, &DeviceMonitor::authorizationRequired, this, &MainWindow::onAuthorizationRequired);
    connect(&m_monitor, &DeviceMonitor::deviceReady, this, &MainWindow::onDeviceReady);
    connect(&m_monitor, &DeviceMonitor::deviceDetached, this, &MainWindow::onDeviceDetached);

    if (!m_adb.isAvailable())
        log(tr("未找到 adb。请安装 platform-tools,或设置环境变量 ADB。"));
    else
        log(tr("adb %1(%2)").arg(m_adb.version(), m_adb.executable()));

    if (!m_probe.isAvailable()) {
        log(tr("未找到 scrcpy。请安装它,或设置环境变量 SCRCPY_EXECUTABLE。"));
    } else {
        log(tr("scrcpy %1(%2)").arg(m_probe.version().toString(), m_probe.executable()));
    }

    const QString cameraProblem = cameraUnavailableReason(0);
    m_modeCamera->setEnabled(cameraProblem.isEmpty());
    m_modeCamera->setToolTip(cameraProblem);

    updateStatus();
    updateOptionStates();
    updateButtons();

    m_monitor.start(); // from now on, plugging in a phone is noticed automatically
}

void MainWindow::buildUi()
{
    // ---- left: status + devices ----
    m_status = new QLabel;
    m_status->setWordWrap(true);
    // A word-wrapped label inside this layout is not given enough height on its own and
    // the authorization hint would be clipped, so reserve room for four lines.
    m_status->setMinimumHeight(m_status->fontMetrics().lineSpacing() * 4 + 20);

    m_deviceList = new QListWidget;
    m_address = new QLineEdit;
    m_address->setPlaceholderText(tr("无线连接,例如 192.168.1.5:5555"));
    m_connectButton = new QPushButton(tr("连接"));
    m_wifiButton = new QPushButton(tr("将所选设备切换到 Wi-Fi"));

    auto *connectRow = new QHBoxLayout;
    connectRow->addWidget(m_address, 1);
    connectRow->addWidget(m_connectButton);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(m_status);
    leftLayout->addWidget(new QLabel(tr("设备")));
    leftLayout->addWidget(m_deviceList, 1);
    leftLayout->addLayout(connectRow);
    leftLayout->addWidget(m_wifiButton);

    // ---- right: mode ----
    m_modeScreen = new QRadioButton(tr("屏幕投屏"));
    m_modeCamera = new QRadioButton(tr("摄像头投屏"));
    m_modeScreen->setChecked(true);
    m_cameraFacing = new QComboBox;
    m_cameraFacing->addItem(tr("后置摄像头"), int(CameraFacing::Back));
    m_cameraFacing->addItem(tr("前置摄像头"), int(CameraFacing::Front));
    m_cameraFacing->addItem(tr("外接摄像头"), int(CameraFacing::External));
    m_cameraFps = new QSpinBox;
    m_cameraFps->setRange(0, 240);
    m_cameraFps->setSpecialValueText(tr("默认"));
    m_askOnReady = new QCheckBox(tr("设备授权后弹出选择窗口"));
    m_askOnReady->setChecked(true);

    auto *modeBox = new QGroupBox(tr("投屏方式"));
    auto *modeForm = new QFormLayout(modeBox);
    modeForm->addRow(m_modeScreen);
    modeForm->addRow(m_modeCamera);
    modeForm->addRow(tr("摄像头"), m_cameraFacing);
    modeForm->addRow(tr("摄像头帧率"), m_cameraFps);
    modeForm->addRow(m_askOnReady);

    // ---- right: options ----
    m_maxSize = new QSpinBox;
    m_maxSize->setRange(0, 8192);
    m_maxSize->setSingleStep(128);
    m_maxSize->setSpecialValueText(tr("原始分辨率"));
    m_bitRate = new QSpinBox;
    m_bitRate->setRange(0, 100);
    m_bitRate->setSuffix(tr(" Mbit/s"));
    m_bitRate->setSpecialValueText(tr("默认"));
    m_maxFps = new QSpinBox;
    m_maxFps->setRange(0, 240);
    m_maxFps->setSpecialValueText(tr("不限制"));
    m_showTouches = new QCheckBox(tr("显示触摸点"));
    m_stayAwake = new QCheckBox(tr("保持手机唤醒"));
    m_turnScreenOff = new QCheckBox(tr("关闭手机屏幕"));
    m_alwaysOnTop = new QCheckBox(tr("窗口置顶"));
    m_noControl = new QCheckBox(tr("仅查看(不能操作手机)"));
    m_noAudio = new QCheckBox(tr("不传输声音"));
    m_noAudio->setToolTip(tr("摄像头投屏默认传输手机麦克风的声音"));
    m_startButton = new QPushButton(tr("开始投屏"));
    m_stopButton = new QPushButton(tr("停止"));

    auto *optionsBox = new QGroupBox(tr("参数"));
    auto *form = new QFormLayout(optionsBox);
    form->addRow(tr("最大尺寸"), m_maxSize);
    form->addRow(tr("码率"), m_bitRate);
    form->addRow(tr("最大帧率"), m_maxFps);
    form->addRow(m_showTouches);
    form->addRow(m_stayAwake);
    form->addRow(m_turnScreenOff);
    form->addRow(m_alwaysOnTop);
    form->addRow(m_noControl);
    form->addRow(m_noAudio);
    auto *startRow = new QHBoxLayout;
    startRow->addWidget(m_startButton, 1);
    startRow->addWidget(m_stopButton);
    form->addRow(startRow);

    // ---- right: adb actions ----
    m_screenshotButton = new QPushButton(tr("截图…"));
    m_installButton = new QPushButton(tr("安装 APK…"));
    m_homeButton = new QPushButton(tr("主页"));
    m_backButton = new QPushButton(tr("返回"));
    m_shellCommand = new QLineEdit;
    m_shellCommand->setPlaceholderText(tr("adb shell 命令,例如 getprop ro.product.model"));
    m_shellButton = new QPushButton(tr("执行"));

    auto *adbBox = new QGroupBox(tr("adb"));
    auto *adbLayout = new QVBoxLayout(adbBox);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_screenshotButton);
    buttonRow->addWidget(m_installButton);
    buttonRow->addWidget(m_homeButton);
    buttonRow->addWidget(m_backButton);
    adbLayout->addLayout(buttonRow);
    auto *shellRow = new QHBoxLayout;
    shellRow->addWidget(m_shellCommand, 1);
    shellRow->addWidget(m_shellButton);
    adbLayout->addLayout(shellRow);

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->addWidget(modeBox);
    rightLayout->addWidget(optionsBox);
    rightLayout->addWidget(adbBox);
    rightLayout->addStretch(1);

    auto *top = new QSplitter;
    top->addWidget(left);
    top->addWidget(right);
    top->setStretchFactor(0, 1);
    top->setStretchFactor(1, 1);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(top, 4);
    layout->addWidget(m_log, 1);
    setCentralWidget(central);

    connect(m_deviceList, &QListWidget::currentRowChanged, this, &MainWindow::updateButtons);
    connect(m_modeScreen, &QRadioButton::toggled, this, &MainWindow::updateOptionStates);
    connect(m_noControl, &QCheckBox::toggled, this, &MainWindow::updateOptionStates);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::connectWireless);
    connect(m_address, &QLineEdit::returnPressed, this, &MainWindow::connectWireless);
    connect(m_wifiButton, &QPushButton::clicked, this, &MainWindow::switchToWifi);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startFromPanel);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopMirroring);
    connect(m_screenshotButton, &QPushButton::clicked, this, &MainWindow::takeScreenshot);
    connect(m_installButton, &QPushButton::clicked, this, &MainWindow::installApk);
    connect(m_shellButton, &QPushButton::clicked, this, &MainWindow::runShellCommand);
    connect(m_shellCommand, &QLineEdit::returnPressed, this, &MainWindow::runShellCommand);
    connect(m_homeButton, &QPushButton::clicked, this, [this] {
        m_adb.shellAsync(selectedSerial(), QStringLiteral("input keyevent %1").arg(int(Adb::Key::Home)),
                         [this](const CommandResult &r) { logResult(tr("主页"), r); });
    });
    connect(m_backButton, &QPushButton::clicked, this, [this] {
        m_adb.shellAsync(selectedSerial(), QStringLiteral("input keyevent %1").arg(int(Adb::Key::Back)),
                         [this](const CommandResult &r) { logResult(tr("返回"), r); });
    });
}

void MainWindow::rebuildDeviceList(const QList<AdbDevice> &devices)
{
    const QString previous = selectedSerial();

    m_deviceList->clear();
    for (const AdbDevice &device : devices) {
        auto *item = new QListWidgetItem(describe(device), m_deviceList);
        item->setData(kSerialRole, device.serial);
        if (!device.isOnline())
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        if (device.serial == previous)
            m_deviceList->setCurrentItem(item);
    }
    if (!m_deviceList->currentItem()) {
        // Prefer a device that can actually be used.
        for (int row = 0; row < devices.size(); ++row) {
            if (devices.at(row).isOnline()) {
                m_deviceList->setCurrentRow(row);
                break;
            }
        }
    }

    updateStatus();
    updateButtons();
}

void MainWindow::updateStatus()
{
    if (!m_adb.isAvailable()) {
        m_status->setText(tr("未找到 adb,请先安装 Android platform-tools。"));
        styleBanner(m_status, QStringLiteral("#fee2e2"), QStringLiteral("#7f1d1d"));
        return;
    }

    int ready = 0, unauthorized = 0, other = 0;
    const QList<AdbDevice> devices = m_monitor.devices();
    for (const AdbDevice &device : devices) {
        if (device.isOnline())
            ++ready;
        else if (device.state == AdbDevice::State::Unauthorized)
            ++unauthorized;
        else
            ++other;
    }

    if (unauthorized > 0) {
        m_status->setText(tr("请在手机上点击“允许 USB 调试”(建议勾选“始终允许”)。\n"
                             "没有弹窗?请确认已开启 USB 调试,或重新插拔数据线。"));
        styleBanner(m_status, QStringLiteral("#fef3c7"), QStringLiteral("#78350f"));
    } else if (ready > 0) {
        m_status->setText(tr("%1 台设备已就绪。选择设备后点“开始投屏”。").arg(ready));
        styleBanner(m_status, QStringLiteral("#dcfce7"), QStringLiteral("#14532d"));
    } else if (other > 0) {
        m_status->setText(tr("设备暂时不可用,请查看下方列表中的提示。"));
        styleBanner(m_status, QStringLiteral("#fef3c7"), QStringLiteral("#78350f"));
    } else {
        m_status->setText(tr("请用数据线连接手机,并在“开发者选项”中打开 USB 调试。"));
        styleBanner(m_status, QStringLiteral("#e0f2fe"), QStringLiteral("#0c4a6e"));
    }
}

QString MainWindow::selectedSerial() const
{
    const QListWidgetItem *item = m_deviceList->currentItem();
    return item ? item->data(kSerialRole).toString() : QString();
}

void MainWindow::updateOptionStates()
{
    const bool camera = m_modeCamera->isChecked();
    m_cameraFacing->setEnabled(camera);
    m_cameraFps->setEnabled(camera);
    m_maxFps->setEnabled(!camera);
    m_noControl->setEnabled(!camera);

    // These need device control, which camera mirroring and "view only" turn off.
    const bool needsControl = !camera && !m_noControl->isChecked();
    for (QCheckBox *box : {m_showTouches, m_stayAwake, m_turnScreenOff})
        box->setEnabled(needsControl);
    const QString why = camera ? tr("摄像头投屏不支持操作手机") : tr("“仅查看”时不能使用");
    for (QCheckBox *box : {m_showTouches, m_stayAwake, m_turnScreenOff})
        box->setToolTip(needsControl ? QString() : why);
}

void MainWindow::updateButtons()
{
    const QString serial = selectedSerial();
    const AdbDevice device = m_monitor.device(serial);
    const bool online = device.isOnline();
    const Scrcpy *session = m_sessions.value(serial);
    const bool mirroring = session && session->state() != Scrcpy::State::Stopped;

    const bool canAct = online && !serial.isEmpty();
    m_startButton->setEnabled(canAct && !mirroring);
    m_stopButton->setEnabled(mirroring);
    for (QPushButton *button : {m_screenshotButton, m_installButton, m_homeButton, m_backButton, m_shellButton})
        button->setEnabled(canAct);
    m_shellCommand->setEnabled(canAct);
    m_wifiButton->setEnabled(canAct && !device.isNetwork());
    m_connectButton->setEnabled(m_adb.isAvailable());
}

void MainWindow::log(const QString &message)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(Qt::ISODate), message));
}

void MainWindow::logResult(const QString &what, const CommandResult &result)
{
    if (result.ok()) {
        const QString text = result.text();
        log(text.isEmpty() ? tr("%1:成功").arg(what) : tr("%1:%2").arg(what, text));
    } else {
        log(tr("%1 失败:%2").arg(what, result.failureReason()));
    }
}

// ---- plug in -> authorize -> choose -----------------------------------------------

void MainWindow::onAuthorizationRequired(const AdbDevice &device)
{
    log(tr("设备 %1 需要授权:请在手机上点击“允许”。").arg(device.serial));
    QApplication::alert(this); // flash the taskbar so the user looks at the phone
}

void MainWindow::onDeviceReady(const AdbDevice &device)
{
    log(tr("设备 %1 已就绪。").arg(device.displayName()));
    if (!m_askOnReady->isChecked())
        return;

    const Scrcpy *session = m_sessions.value(device.serial);
    if (session && session->state() != Scrcpy::State::Stopped)
        return; // already mirroring this one
    if (device.serial == m_promptSerial || m_promptQueue.contains(device.serial))
        return;

    m_promptQueue << device.serial;
    showNextPrompt();
}

void MainWindow::onDeviceDetached(const AdbDevice &device)
{
    log(tr("设备 %1 已断开。").arg(device.serial));
    m_promptQueue.removeAll(device.serial);
    if (m_prompt && m_promptSerial == device.serial)
        m_prompt->reject();
}

void MainWindow::showNextPrompt()
{
    if (m_promptBusy || m_promptQueue.isEmpty())
        return;
    m_promptBusy = true;
    m_promptSerial = m_promptQueue.takeFirst();

    // The Android version decides whether camera mirroring is offered.
    const QString serial = m_promptSerial;
    m_adb.androidSdkVersionAsync(serial, [this, serial](int sdk) { openPrompt(serial, sdk); });
}

void MainWindow::openPrompt(const QString &serial, int androidSdk)
{
    // The phone may have been unplugged while its Android version was being read.
    if (m_promptSerial != serial || !m_monitor.device(serial).isOnline()) {
        finishPrompt();
        return;
    }

    const AdbDevice device = m_monitor.device(serial);
    auto *dialog = new ModeDialog(device.displayName(), serial, cameraUnavailableReason(androidSdk), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_prompt = dialog;
    connect(dialog, &QDialog::finished, this, [this, dialog, serial](int result) {
        if (result == QDialog::Accepted && dialog->choice() != ModeDialog::Choice::None) {
            const bool camera = dialog->choice() == ModeDialog::Choice::Camera;
            startMirroring(serial, camera ? VideoSource::Camera : VideoSource::Display, dialog->cameraFacing());
        }
        finishPrompt();
    });
    dialog->open();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::finishPrompt()
{
    m_promptBusy = false;
    m_promptSerial.clear();
    QTimer::singleShot(0, this, &MainWindow::showNextPrompt); // next device, if several were plugged in
}

QString MainWindow::cameraUnavailableReason(int androidSdk) const
{
    if (!m_probe.isAvailable())
        return tr("未找到 scrcpy。");
    const ScrcpyVersion version = m_probe.version();
    if (version.isValid() && version < ScrcpyOptions::cameraMinimumScrcpy()) {
        return tr("摄像头投屏需要 scrcpy %1 或更高版本(当前 %2)。")
            .arg(ScrcpyOptions::cameraMinimumScrcpy().toString(), version.toString());
    }
    if (androidSdk > 0 && androidSdk < ScrcpyOptions::kCameraMinimumSdk)
        return tr("摄像头投屏需要 Android 12 或更高版本(此设备为 API %1)。").arg(androidSdk);
    return {};
}

// ---- scrcpy ---------------------------------------------------------------------

ScrcpyOptions MainWindow::currentOptions(const QString &serial, VideoSource source, CameraFacing facing) const
{
    const bool camera = source == VideoSource::Camera;
    ScrcpyOptions options = camera ? ScrcpyOptions::forCamera(serial, facing) : ScrcpyOptions::forScreen(serial);

    options.maxSize = m_maxSize->value();
    options.bitRate = m_bitRate->value() * 1000000;
    options.alwaysOnTop = m_alwaysOnTop->isChecked();
    options.noAudio = m_noAudio->isChecked();
    options.windowTitle = QStringLiteral("%1 - %2").arg(m_monitor.device(serial).displayName(),
                                                        camera ? tr("摄像头") : tr("屏幕"));
    if (camera) {
        options.cameraFps = m_cameraFps->value();
    } else {
        options.maxFps = m_maxFps->value();
        options.showTouches = m_showTouches->isChecked();
        options.stayAwake = m_stayAwake->isChecked();
        options.turnScreenOff = m_turnScreenOff->isChecked();
        options.noControl = m_noControl->isChecked();
    }
    return options;
}

Scrcpy *MainWindow::sessionFor(const QString &serial)
{
    if (Scrcpy *existing = m_sessions.value(serial))
        return existing;

    auto *session = new Scrcpy(this);
    session->setAdbExecutable(m_adb.executable()); // make scrcpy use the same adb as we do
    m_sessions.insert(serial, session);

    const auto prefix = [serial] { return QStringLiteral("scrcpy[%1]").arg(serial); };
    connect(session, &Scrcpy::stateChanged, this, &MainWindow::updateButtons);
    connect(session, &Scrcpy::errorOccurred, this, [this, prefix](const QString &message) {
        log(QStringLiteral("%1: %2").arg(prefix(), message));
    });
    connect(session, &Scrcpy::outputReceived, this, [this, prefix](const QString &line, bool) {
        if (!line.isEmpty())
            log(QStringLiteral("%1: %2").arg(prefix(), line));
    });
    connect(session, &Scrcpy::finished, this, [this, prefix](int exitCode, bool crashed) {
        log(crashed ? tr("%1 异常退出").arg(prefix()) : tr("%1 已退出(退出码 %2)").arg(prefix()).arg(exitCode));
    });
    return session;
}

void MainWindow::startMirroring(const QString &serial, VideoSource source, CameraFacing facing)
{
    if (source == VideoSource::Camera) {
        const QString reason = cameraUnavailableReason(m_adb.androidSdkVersion(serial));
        if (!reason.isEmpty()) {
            log(reason);
            return;
        }
    }

    Scrcpy *session = sessionFor(serial);
    if (session->state() != Scrcpy::State::Stopped) {
        log(tr("设备 %1 已经在投屏了。").arg(serial));
        return;
    }
    if (session->start(currentOptions(serial, source, facing))) {
        log(tr("开始%1:scrcpy %2 %3")
                .arg(source == VideoSource::Camera ? tr("摄像头投屏") : tr("屏幕投屏"),
                     session->version().toString(), session->arguments().join(QLatin1Char(' '))));
    }
    updateButtons();
}

void MainWindow::startFromPanel()
{
    const bool camera = m_modeCamera->isChecked();
    startMirroring(selectedSerial(), camera ? VideoSource::Camera : VideoSource::Display,
                   static_cast<CameraFacing>(m_cameraFacing->currentData().toInt()));
}

void MainWindow::stopMirroring()
{
    if (Scrcpy *session = m_sessions.value(selectedSerial()))
        session->stop();
}

// ---- wireless -------------------------------------------------------------------

void MainWindow::connectWireless()
{
    const QString address = m_address->text().trimmed();
    if (address.isEmpty())
        return;
    log(tr("正在连接 %1…").arg(address));
    m_adb.connectDeviceAsync(address, [this](const CommandResult &r) {
        logResult(tr("连接"), r);
        m_adb.refreshDevices();
    });
}

void MainWindow::switchToWifi()
{
    const QString serial = selectedSerial();
    // The (quick) blocking calls are fine here: the address must be known before connecting.
    const QString ip = m_adb.deviceIpAddress(serial);
    if (ip.isEmpty()) {
        log(tr("无法获取 %1 的 Wi-Fi 地址(Wi-Fi 是否已开启?)").arg(serial));
        return;
    }
    const CommandResult tcpip = m_adb.tcpip(serial);
    if (!tcpip.ok()) {
        logResult(tr("tcpip"), tcpip);
        return;
    }
    const QString address = ip + QStringLiteral(":5555");
    m_address->setText(address);
    log(tr("%1 已切换到 TCP/IP 模式,正在连接 %2…").arg(serial, address));
    // adbd restarts, so give it a moment before connecting.
    QTimer::singleShot(1500, this, [this, address] {
        m_adb.connectDeviceAsync(address, [this](const CommandResult &r) {
            logResult(tr("连接"), r);
            m_adb.refreshDevices();
        });
    });
}

// ---- adb actions ----------------------------------------------------------------

void MainWindow::takeScreenshot()
{
    const QString serial = selectedSerial();
    m_adb.screenshotAsync(serial, [this](const CommandResult &r) {
        if (!r.ok()) {
            logResult(tr("截图"), r);
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, tr("保存截图"), QStringLiteral("screenshot.png"),
                                                          tr("PNG 图片 (*.png)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(r.standardOutput) == r.standardOutput.size())
            log(tr("已保存 %1").arg(path));
        else
            log(tr("无法写入 %1:%2").arg(path, file.errorString()));
    });
}

void MainWindow::installApk()
{
    const QString serial = selectedSerial();
    const QString apk = QFileDialog::getOpenFileName(this, tr("安装 APK"), {}, tr("Android 安装包 (*.apk)"));
    if (apk.isEmpty())
        return;
    log(tr("正在安装 %1…").arg(apk));
    m_adb.installAsync(serial, apk, [this](const CommandResult &r) { logResult(tr("安装"), r); });
}

void MainWindow::runShellCommand()
{
    const QString command = m_shellCommand->text().trimmed();
    if (command.isEmpty())
        return;
    log(QStringLiteral("$ %1").arg(command));
    m_adb.shellAsync(selectedSerial(), command, [this](const CommandResult &r) {
        const QString output = QString::fromUtf8(r.standardOutput).trimmed();
        if (!output.isEmpty())
            log(output);
        if (!r.ok())
            log(tr("失败:%1").arg(r.failureReason()));
    });
}
