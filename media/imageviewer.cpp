#include "imageviewer.h"
#include <QImageReader>
#include <QDebug>

ImageViewer::ImageViewer(QObject *parent) : IMediaPlayer(parent) {}

bool ImageViewer::load(const QString &filePath)
{
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        emit errorOccurred(QStringLiteral("无法加载图片: %1").arg(reader.errorString()));
        return false;
    }
    m_image = img;
    emit frameChanged(m_image);
    emit durationChanged(0);
    emit positionChanged(0);
    emit stoppedStateChanged(true);
    emit pausedStateChanged(false);
    emit playingStateChanged(false);
    return true;
}

QImage ImageViewer::currentFrame() const
{
    return m_image;
}
