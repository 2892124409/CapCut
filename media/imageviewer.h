#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include "imediaplayer.h"
#include "imagedecoder.h"
#include <QReadWriteLock>
#include <QMatrix4x4>

/**
 * @brief 图片查看器实现
 * 
 * 基于原有ImageDecoder重构，实现IMediaPlayer接口
 */
class ImageViewer : public IMediaPlayer {
    Q_OBJECT

public:
    explicit ImageViewer(QObject *parent = nullptr);
    ~ImageViewer() override;

    // IMediaPlayer 接口实现
    bool load(const QString &filePath) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 position) override;
    void setVolume(float volume) override;

    qint64 duration() const override;
    qint64 position() const override;
    bool isPlaying() const override;
    bool isPaused() const override;
    bool isStopped() const override;
    QString mediaType() const override;

    QImage currentFrame() const override;
    bool supportsZoom() const override;
    bool supportsRotation() const override;
    qreal zoomLevel() const override;
    qreal rotationAngle() const override;
    void setZoomLevel(qreal zoom) override;
    void setRotationAngle(qreal angle) override;
    void resetTransform() override;

private:
    // 私有方法
    void updateTransformMatrix();

    // 图片解码器
    ImageDecoder *m_imageDecoder = nullptr;
    
    // 状态变量
    bool m_isLoaded = false;
    qreal m_zoomLevel = 1.0;
    qreal m_rotationAngle = 0.0;
    QMatrix4x4 m_transformMatrix;
};

#endif // IMAGEVIEWER_H
