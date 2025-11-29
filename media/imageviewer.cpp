#include "imageviewer.h"
#include <QDebug>

ImageViewer::ImageViewer(QObject *parent)
    : IMediaPlayer(parent)
{
    m_imageDecoder = new ImageDecoder(this);
}

ImageViewer::~ImageViewer()
{
    if (m_imageDecoder) {
        delete m_imageDecoder;
        m_imageDecoder = nullptr;
    }
}

bool ImageViewer::load(const QString &filePath)
{
    if (!m_imageDecoder) {
        return false;
    }

    bool success = m_imageDecoder->loadImage(filePath);
    if (success) {
        m_isLoaded = true;
        m_zoomLevel = 1.0;
        m_rotationAngle = 0.0;
        updateTransformMatrix();
        
        emit durationChanged(0);
        emit positionChanged(0);
        emit playingStateChanged(false);
        emit pausedStateChanged(false);
        emit stoppedStateChanged(true);
        emit frameChanged(m_imageDecoder->getCurrentImage());
        emit zoomLevelChanged(m_zoomLevel);
        emit rotationAngleChanged(m_rotationAngle);
    }
    
    return success;
}

void ImageViewer::play()
{
    // 图片查看器不支持播放
    qDebug() << "ImageViewer: 图片查看器不支持播放操作";
}

void ImageViewer::pause()
{
    // 图片查看器不支持暂停
    qDebug() << "ImageViewer: 图片查看器不支持暂停操作";
}

void ImageViewer::stop()
{
    // 图片查看器不支持停止
    qDebug() << "ImageViewer: 图片查看器不支持停止操作";
}

void ImageViewer::seek(qint64 position)
{
    // 图片查看器不支持跳转
    qDebug() << "ImageViewer: 图片查看器不支持跳转操作";
}

void ImageViewer::setVolume(float volume)
{
    // 图片查看器不支持音量控制
    qDebug() << "ImageViewer: 图片查看器不支持音量控制";
}

qint64 ImageViewer::duration() const
{
    return 0; // 图片没有时长
}

qint64 ImageViewer::position() const
{
    return 0; // 图片没有播放位置
}

bool ImageViewer::isPlaying() const
{
    return false; // 图片查看器不处于播放状态
}

bool ImageViewer::isPaused() const
{
    return false; // 图片查看器不处于暂停状态
}

bool ImageViewer::isStopped() const
{
    return true; // 图片查看器始终处于停止状态
}

QString ImageViewer::mediaType() const
{
    return "image";
}

QImage ImageViewer::currentFrame() const
{
    if (m_imageDecoder) {
        return m_imageDecoder->getCurrentImage();
    }
    return QImage();
}

bool ImageViewer::supportsZoom() const
{
    return true; // 图片支持缩放
}

bool ImageViewer::supportsRotation() const
{
    return true; // 图片支持旋转
}

qreal ImageViewer::zoomLevel() const
{
    return m_zoomLevel;
}

qreal ImageViewer::rotationAngle() const
{
    return m_rotationAngle;
}

void ImageViewer::setZoomLevel(qreal zoom)
{
    if (zoom > 0.1 && zoom < 10.0) {
        m_zoomLevel = zoom;
        updateTransformMatrix();
        emit zoomLevelChanged(m_zoomLevel);
        emit frameChanged(currentFrame());
    }
}

void ImageViewer::setRotationAngle(qreal angle)
{
    m_rotationAngle = angle;
    updateTransformMatrix();
    emit rotationAngleChanged(m_rotationAngle);
    emit frameChanged(currentFrame());
}

void ImageViewer::resetTransform()
{
    m_zoomLevel = 1.0;
    m_rotationAngle = 0.0;
    updateTransformMatrix();
    emit zoomLevelChanged(m_zoomLevel);
    emit rotationAngleChanged(m_rotationAngle);
    emit frameChanged(currentFrame());
}

void ImageViewer::updateTransformMatrix()
{
    m_transformMatrix.setToIdentity();
    
    // 应用缩放
    m_transformMatrix.scale(m_zoomLevel, m_zoomLevel);
    
    // 应用旋转
    m_transformMatrix.rotate(m_rotationAngle, 0, 0, 1);
}
