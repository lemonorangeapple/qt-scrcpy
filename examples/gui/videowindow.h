#pragma once

#include <qtscrcpy/h264decoder.h>

#include <QImage>
#include <QWidget>

// Shows decoded video frames, scaled to the window while keeping the aspect ratio.
class VideoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWindow(const QString &title, QWidget *parent = nullptr);

    void showFrame(const QtScrcpy::VideoFrame &frame);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QImage m_image;
};
