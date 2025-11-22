#ifndef IMAGEDECODER_H
#define IMAGEDECODER_H

#include <QObject>
#include <QImage>
#include <QReadWriteLock>
#include <QMatrix4x4>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

class ImageDecoder : public QObject
{
    Q_OBJECT

public:
    explicit ImageDecoder(QObject *parent = nullptr);
    ~ImageDecoder();

    // 图片加载和解码
    bool loadImage(const QString& filePath);
    QImage getCurrentImage() const;
    void cleanup();

    // 图片操作
    void setZoomLevel(qreal zoom);
    void setRotationAngle(qreal angle);
    void resetTransform();
    
    // 属性读取器
    qreal zoomLevel() const { return m_zoomLevel; }
    qreal rotationAngle() const { return m_rotationAngle; }
    QMatrix4x4 transformMatrix() const { return m_transformMatrix; }

private:
    void updateTransformMatrix();
    QImage convertAVFrameToQImage(AVFrame* frame);

    QImage m_currentImage;
    mutable QReadWriteLock m_imageLock;
    
    // 图片变换参数
    qreal m_zoomLevel = 1.0;
    qreal m_rotationAngle = 0.0;
    QMatrix4x4 m_transformMatrix;
    
    // FFmpeg相关
    AVFormatContext* m_formatCtx = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    SwsContext* m_swsCtx = nullptr;
    int m_streamIndex = -1;
};

#endif // IMAGEDECODER_H
