#include "videowindow.h"

#include <QCloseEvent>
#include <QPainter>

VideoWindow::VideoWindow(const QString &title, QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setWindowTitle(title);
    setMinimumSize(160, 160);
    resize(405, 720);
}

void VideoWindow::showFrame(const QtScrcpy::VideoFrame &frame)
{
    if (frame.width <= 0 || frame.height <= 0 || frame.bgra.size() != frame.width * frame.height * 4)
        return;
    const bool firstOrResized = m_image.isNull() || m_image.width() != frame.width || m_image.height() != frame.height;
    // The image refers to the frame's bytes: copy once so it stays valid after the signal returns.
    m_image = QImage(reinterpret_cast<const uchar *>(frame.bgra.constData()), frame.width, frame.height,
                     frame.width * 4, QImage::Format_ARGB32).copy();
    if (firstOrResized && frame.width > 0) {
        // Follow the picture's shape (e.g. after the phone is rotated), keeping the current area.
        const double aspect = double(frame.width) / frame.height;
        const int height = qMax(160, this->height());
        resize(int(height * aspect), height);
    }
    update();
}

void VideoWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (m_image.isNull())
        return;
    QSize target = m_image.size();
    target.scale(size(), Qt::KeepAspectRatio);
    const QRect area(QPoint((width() - target.width()) / 2, (height() - target.height()) / 2), target);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(area, m_image);
}

void VideoWindow::closeEvent(QCloseEvent *event)
{
    emit closed();
    QWidget::closeEvent(event);
}
