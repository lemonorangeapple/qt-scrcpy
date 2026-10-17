#pragma once

#include <qtscrcpy/adb.h>
#include <qtscrcpy/devicemonitor.h>
#include <qtscrcpy/scrcpy.h>

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>

class ModeDialog;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

// Demo front-end for the qtscrcpy library.
//
// Intended flow: plug in the cable, tap "Allow" on the phone's USB debugging prompt, then
// pick screen mirroring or camera mirroring on the computer.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    void rebuildDeviceList(const QList<QtScrcpy::AdbDevice> &devices);
    void updateStatus();
    void updateButtons();
    void updateOptionStates();
    void log(const QString &message);
    QString selectedSerial() const;
    QtScrcpy::ScrcpyOptions currentOptions(const QString &serial, QtScrcpy::VideoSource source,
                                           QtScrcpy::CameraFacing facing) const;
    QtScrcpy::Scrcpy *sessionFor(const QString &serial);
    QString cameraUnavailableReason(int androidSdk) const;

    // authorization / device ready flow
    void onAuthorizationRequired(const QtScrcpy::AdbDevice &device);
    void onDeviceReady(const QtScrcpy::AdbDevice &device);
    void onDeviceDetached(const QtScrcpy::AdbDevice &device);
    void showNextPrompt();
    void openPrompt(const QString &serial, int androidSdk);
    void finishPrompt();

    void startMirroring(const QString &serial, QtScrcpy::VideoSource source, QtScrcpy::CameraFacing facing);
    void startFromPanel();
    void stopMirroring();

    void connectWireless();
    void switchToWifi();
    void takeScreenshot();
    void installApk();
    void runShellCommand();
    void logResult(const QString &what, const QtScrcpy::CommandResult &result);

    QtScrcpy::Adb m_adb;
    QtScrcpy::DeviceMonitor m_monitor{&m_adb};
    QtScrcpy::Scrcpy m_probe; // only used to ask for the scrcpy version / availability
    QHash<QString, QtScrcpy::Scrcpy *> m_sessions; // one scrcpy per device serial

    QStringList m_promptQueue;
    bool m_promptBusy = false;
    QString m_promptSerial;
    QPointer<ModeDialog> m_prompt;

    QLabel *m_status = nullptr;
    QListWidget *m_deviceList = nullptr;
    QLineEdit *m_address = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_wifiButton = nullptr;

    QRadioButton *m_modeScreen = nullptr;
    QRadioButton *m_modeCamera = nullptr;
    QComboBox *m_cameraFacing = nullptr;
    QSpinBox *m_cameraFps = nullptr;
    QCheckBox *m_askOnReady = nullptr;

    QSpinBox *m_maxSize = nullptr;
    QSpinBox *m_bitRate = nullptr;
    QSpinBox *m_maxFps = nullptr;
    QCheckBox *m_showTouches = nullptr;
    QCheckBox *m_stayAwake = nullptr;
    QCheckBox *m_turnScreenOff = nullptr;
    QCheckBox *m_alwaysOnTop = nullptr;
    QCheckBox *m_noControl = nullptr;
    QCheckBox *m_noAudio = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;

    QPushButton *m_screenshotButton = nullptr;
    QPushButton *m_installButton = nullptr;
    QPushButton *m_homeButton = nullptr;
    QPushButton *m_backButton = nullptr;
    QLineEdit *m_shellCommand = nullptr;
    QPushButton *m_shellButton = nullptr;

    QPlainTextEdit *m_log = nullptr;
};
