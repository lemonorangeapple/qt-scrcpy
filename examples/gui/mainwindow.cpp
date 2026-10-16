#include "mainwindow.h"

#include <QCheckBox>
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
#include <QSpinBox>
#include <QSplitter>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

using namespace QtScrcpy;

namespace {
constexpr int kSerialRole = Qt::UserRole;

QString describe(const AdbDevice &device)
{
    QString text = QStringLiteral("%1\n%2").arg(device.displayName(), device.serial);
    if (!device.isOnline())
        text += QStringLiteral("  [%1]").arg(device.stateText);
    return text;
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("qt-scrcpy"));
    resize(900, 640);
    buildUi();

    connect(&m_adb, &Adb::devicesChanged, this, &MainWindow::rebuildDeviceList);
    connect(&m_adb, &Adb::errorOccurred, this, [this](const QString &message) {
        log(tr("adb: %1").arg(message));
    });

    if (!m_adb.isAvailable())
        log(tr("adb not found. Install platform-tools or set $ADB."));
    else
        log(tr("adb %1 (%2)").arg(m_adb.version(), m_adb.executable()));

    if (!sessionFor({})->isAvailable())
        log(tr("scrcpy not found. Install it or set $SCRCPY_EXECUTABLE."));

    m_adb.startTracking(); // device list follows plug/unplug events from now on
    updateButtons();
}

void MainWindow::buildUi()
{
    // ---- left: devices ----
    m_deviceList = new QListWidget;
    m_address = new QLineEdit;
    m_address->setPlaceholderText(tr("192.168.1.5:5555"));
    m_connectButton = new QPushButton(tr("Connect"));
    m_wifiButton = new QPushButton(tr("Switch selected device to Wi-Fi"));

    auto *connectRow = new QHBoxLayout;
    connectRow->addWidget(m_address, 1);
    connectRow->addWidget(m_connectButton);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->addWidget(new QLabel(tr("Devices")));
    leftLayout->addWidget(m_deviceList, 1);
    leftLayout->addLayout(connectRow);
    leftLayout->addWidget(m_wifiButton);

    // ---- right: scrcpy options ----
    m_maxSize = new QSpinBox;
    m_maxSize->setRange(0, 8192);
    m_maxSize->setSingleStep(128);
    m_maxSize->setSpecialValueText(tr("device resolution"));
    m_bitRate = new QSpinBox;
    m_bitRate->setRange(0, 100);
    m_bitRate->setSuffix(tr(" Mbit/s"));
    m_bitRate->setSpecialValueText(tr("default"));
    m_maxFps = new QSpinBox;
    m_maxFps->setRange(0, 240);
    m_maxFps->setSpecialValueText(tr("unlimited"));
    m_showTouches = new QCheckBox(tr("Show touches"));
    m_stayAwake = new QCheckBox(tr("Stay awake"));
    m_turnScreenOff = new QCheckBox(tr("Turn device screen off"));
    m_alwaysOnTop = new QCheckBox(tr("Window always on top"));
    m_noControl = new QCheckBox(tr("View only (no control)"));
    m_startButton = new QPushButton(tr("Start mirroring"));
    m_stopButton = new QPushButton(tr("Stop"));

    auto *scrcpyBox = new QGroupBox(tr("scrcpy"));
    auto *form = new QFormLayout(scrcpyBox);
    form->addRow(tr("Max size"), m_maxSize);
    form->addRow(tr("Bit rate"), m_bitRate);
    form->addRow(tr("Max FPS"), m_maxFps);
    form->addRow(m_showTouches);
    form->addRow(m_stayAwake);
    form->addRow(m_turnScreenOff);
    form->addRow(m_alwaysOnTop);
    form->addRow(m_noControl);
    auto *startRow = new QHBoxLayout;
    startRow->addWidget(m_startButton, 1);
    startRow->addWidget(m_stopButton);
    form->addRow(startRow);

    // ---- right: adb actions ----
    m_screenshotButton = new QPushButton(tr("Screenshot…"));
    m_installButton = new QPushButton(tr("Install APK…"));
    m_homeButton = new QPushButton(tr("Home"));
    m_backButton = new QPushButton(tr("Back"));
    m_shellCommand = new QLineEdit;
    m_shellCommand->setPlaceholderText(tr("adb shell command, e.g. getprop ro.product.model"));
    m_shellButton = new QPushButton(tr("Run"));

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
    rightLayout->addWidget(scrcpyBox);
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
    layout->addWidget(top, 3);
    layout->addWidget(m_log, 2);
    setCentralWidget(central);

    connect(m_deviceList, &QListWidget::currentRowChanged, this, &MainWindow::updateButtons);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::connectWireless);
    connect(m_address, &QLineEdit::returnPressed, this, &MainWindow::connectWireless);
    connect(m_wifiButton, &QPushButton::clicked, this, &MainWindow::switchToWifi);
    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startMirroring);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopMirroring);
    connect(m_screenshotButton, &QPushButton::clicked, this, &MainWindow::takeScreenshot);
    connect(m_installButton, &QPushButton::clicked, this, &MainWindow::installApk);
    connect(m_shellButton, &QPushButton::clicked, this, &MainWindow::runShellCommand);
    connect(m_shellCommand, &QLineEdit::returnPressed, this, &MainWindow::runShellCommand);
    connect(m_homeButton, &QPushButton::clicked, this, [this] {
        m_adb.shellAsync(selectedSerial(), QStringLiteral("input keyevent %1").arg(int(Adb::Key::Home)),
                         [this](const CommandResult &r) { logResult(tr("Home"), r); });
    });
    connect(m_backButton, &QPushButton::clicked, this, [this] {
        m_adb.shellAsync(selectedSerial(), QStringLiteral("input keyevent %1").arg(int(Adb::Key::Back)),
                         [this](const CommandResult &r) { logResult(tr("Back"), r); });
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
    if (!m_deviceList->currentItem() && m_deviceList->count() > 0)
        m_deviceList->setCurrentRow(0);

    log(tr("%n device(s) connected", nullptr, devices.size()));
    updateButtons();
}

QString MainWindow::selectedSerial() const
{
    const QListWidgetItem *item = m_deviceList->currentItem();
    return item ? item->data(kSerialRole).toString() : QString();
}

void MainWindow::updateButtons()
{
    const QString serial = selectedSerial();
    bool online = false;
    for (const AdbDevice &device : m_adb.lastKnownDevices()) {
        if (device.serial == serial)
            online = device.isOnline();
    }
    const Scrcpy *session = m_sessions.value(serial);
    const bool mirroring = session && session->state() != Scrcpy::State::Stopped;

    const bool canAct = online && !serial.isEmpty();
    m_startButton->setEnabled(canAct && !mirroring);
    m_stopButton->setEnabled(mirroring);
    for (QPushButton *button : {m_screenshotButton, m_installButton, m_homeButton, m_backButton, m_shellButton})
        button->setEnabled(canAct);
    m_shellCommand->setEnabled(canAct);
    m_wifiButton->setEnabled(canAct);
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
        log(text.isEmpty() ? tr("%1: ok").arg(what) : tr("%1: %2").arg(what, text));
    } else {
        log(tr("%1 failed: %2").arg(what, result.failureReason()));
    }
}

// ---- wireless -------------------------------------------------------------------

void MainWindow::connectWireless()
{
    const QString address = m_address->text().trimmed();
    if (address.isEmpty())
        return;
    log(tr("Connecting to %1…").arg(address));
    m_adb.connectDeviceAsync(address, [this](const CommandResult &r) {
        logResult(tr("connect"), r);
        m_adb.refreshDevices();
    });
}

void MainWindow::switchToWifi()
{
    const QString serial = selectedSerial();
    // Needs the (quick) blocking calls: the address must be known before connecting.
    const QString ip = m_adb.deviceIpAddress(serial);
    if (ip.isEmpty()) {
        log(tr("Could not determine the Wi-Fi address of %1 (is Wi-Fi on?)").arg(serial));
        return;
    }
    const CommandResult tcpip = m_adb.tcpip(serial);
    if (!tcpip.ok()) {
        logResult(tr("tcpip"), tcpip);
        return;
    }
    const QString address = ip + QStringLiteral(":5555");
    m_address->setText(address);
    log(tr("%1 now listens on %2; connecting…").arg(serial, address));
    // adbd restarts, so give it a moment before connecting.
    QTimer::singleShot(1500, this, [this, address] {
        m_adb.connectDeviceAsync(address, [this](const CommandResult &r) {
            logResult(tr("connect"), r);
            m_adb.refreshDevices();
        });
    });
}

// ---- scrcpy ---------------------------------------------------------------------

ScrcpyOptions MainWindow::currentOptions() const
{
    ScrcpyOptions options;
    options.serial = selectedSerial();
    options.maxSize = m_maxSize->value();
    options.bitRate = m_bitRate->value() * 1000000;
    options.maxFps = m_maxFps->value();
    options.showTouches = m_showTouches->isChecked();
    options.stayAwake = m_stayAwake->isChecked();
    options.turnScreenOff = m_turnScreenOff->isChecked();
    options.alwaysOnTop = m_alwaysOnTop->isChecked();
    options.noControl = m_noControl->isChecked();
    options.windowTitle = selectedSerial();
    return options;
}

Scrcpy *MainWindow::sessionFor(const QString &serial)
{
    if (Scrcpy *existing = m_sessions.value(serial))
        return existing;

    auto *session = new Scrcpy(this);
    session->setAdbExecutable(m_adb.executable()); // make scrcpy use the same adb as we do
    m_sessions.insert(serial, session);

    const auto prefix = [serial] { return serial.isEmpty() ? QStringLiteral("scrcpy") : QStringLiteral("scrcpy[%1]").arg(serial); };
    connect(session, &Scrcpy::stateChanged, this, &MainWindow::updateButtons);
    connect(session, &Scrcpy::errorOccurred, this, [this, prefix](const QString &message) {
        log(QStringLiteral("%1: %2").arg(prefix(), message));
    });
    connect(session, &Scrcpy::outputReceived, this, [this, prefix](const QString &line, bool) {
        if (!line.isEmpty())
            log(QStringLiteral("%1: %2").arg(prefix(), line));
    });
    connect(session, &Scrcpy::finished, this, [this, prefix](int exitCode, bool crashed) {
        log(crashed ? tr("%1 crashed").arg(prefix()) : tr("%1 exited with code %2").arg(prefix()).arg(exitCode));
    });
    return session;
}

void MainWindow::startMirroring()
{
    const QString serial = selectedSerial();
    Scrcpy *session = sessionFor(serial);
    if (session->start(currentOptions()))
        log(tr("scrcpy %1 %2").arg(session->version().toString(), session->arguments().join(QLatin1Char(' '))));
    updateButtons();
}

void MainWindow::stopMirroring()
{
    if (Scrcpy *session = m_sessions.value(selectedSerial()))
        session->stop();
}

// ---- adb actions ----------------------------------------------------------------

void MainWindow::takeScreenshot()
{
    const QString serial = selectedSerial();
    m_adb.screenshotAsync(serial, [this](const CommandResult &r) {
        if (!r.ok()) {
            logResult(tr("screenshot"), r);
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, tr("Save screenshot"),
                                                          QStringLiteral("screenshot.png"), tr("PNG image (*.png)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(r.standardOutput) == r.standardOutput.size())
            log(tr("Saved %1").arg(path));
        else
            log(tr("Could not write %1: %2").arg(path, file.errorString()));
    });
}

void MainWindow::installApk()
{
    const QString serial = selectedSerial();
    const QString apk = QFileDialog::getOpenFileName(this, tr("Install APK"), {}, tr("Android package (*.apk)"));
    if (apk.isEmpty())
        return;
    log(tr("Installing %1…").arg(apk));
    m_adb.installAsync(serial, apk, [this](const CommandResult &r) { logResult(tr("install"), r); });
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
            log(tr("failed: %1").arg(r.failureReason()));
    });
}
