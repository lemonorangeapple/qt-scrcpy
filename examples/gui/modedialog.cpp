#include "modedialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ModeDialog::ModeDialog(const QString &deviceName, const QString &serial, const QString &cameraUnavailableReason,
                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("选择投屏方式"));
    setModal(true);

    auto *title = new QLabel(tr("<b>%1</b> 已授权,可以使用了").arg(deviceName.toHtmlEscaped()));
    auto *subtitle = new QLabel(serial);
    subtitle->setEnabled(false);

    auto *screenButton = new QPushButton(tr("屏幕投屏\n在电脑上显示并操作手机屏幕"));
    auto *cameraButton = new QPushButton(tr("摄像头投屏\n把手机摄像头画面显示到电脑"));
    for (QPushButton *button : {screenButton, cameraButton})
        button->setMinimumHeight(64);
    screenButton->setDefault(true);

    m_facing = new QComboBox;
    m_facing->addItem(tr("后置摄像头"), int(QtScrcpy::CameraFacing::Back));
    m_facing->addItem(tr("前置摄像头"), int(QtScrcpy::CameraFacing::Front));
    m_facing->addItem(tr("外接摄像头"), int(QtScrcpy::CameraFacing::External));

    auto *cameraRow = new QHBoxLayout;
    cameraRow->addWidget(new QLabel(tr("摄像头投屏使用:")));
    cameraRow->addWidget(m_facing, 1);

    auto *cancel = new QPushButton(tr("取消"));
    auto *cancelRow = new QHBoxLayout;
    cancelRow->addStretch(1);
    cancelRow->addWidget(cancel);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(8);
    layout->addWidget(screenButton);
    layout->addWidget(cameraButton);
    layout->addLayout(cameraRow);
    if (!cameraUnavailableReason.isEmpty()) {
        auto *reason = new QLabel(cameraUnavailableReason);
        reason->setWordWrap(true);
        reason->setStyleSheet(QStringLiteral("color: #b45309;"));
        layout->addWidget(reason);
        cameraButton->setEnabled(false);
        m_facing->setEnabled(false);
    }
    layout->addLayout(cancelRow);

    connect(screenButton, &QPushButton::clicked, this, [this] {
        m_choice = Choice::Screen;
        accept();
    });
    connect(cameraButton, &QPushButton::clicked, this, [this] {
        m_choice = Choice::Camera;
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

QtScrcpy::CameraFacing ModeDialog::cameraFacing() const
{
    return static_cast<QtScrcpy::CameraFacing>(m_facing->currentData().toInt());
}
