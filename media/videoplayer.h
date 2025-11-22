#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H

#include <QQuickItem>
#include <QTimer>
#include <QElapsedTimer>
#include <QReadWriteLock>
#include <QThread>
#include <QImage>
#include <atomic>
#include <QSGTexture>
#include <QSGSimpleTextureNode>
#include <QtQml/qqmlregistration.h>
#include <QMatrix4x4>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 前向声明
class DecoderWorker;
class AudioDecoder;
class VideoDecoder;
class ImageDecoder;

class VideoPlayer : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    // === 暴露给 QML 的属性 ===
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedChanged)
    Q_PROPERTY(qreal zoomLevel READ zoomLevel NOTIFY zoomLevelChanged)
    Q_PROPERTY(qreal rotationAngle READ rotationAngle NOTIFY rotationAngleChanged)
    Q_PROPERTY(QString currentMediaType READ currentMediaType NOTIFY currentMediaTypeChanged)

public:
    explicit VideoPlayer(QQuickItem *parent = nullptr);
    ~VideoPlayer() override;

    // OpenGL 渲染入口
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

    // === 供 QML 调用的接口 ===
    Q_INVOKABLE void play(QString filePath);
    Q_INVOKABLE void loadImage(QString filePath);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void setVolume(float volume);
    
    // 图片操作接口
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void resetZoom();
    Q_INVOKABLE void rotateLeft();
    Q_INVOKABLE void rotateRight();

    // 属性读取器
    qint64 duration() const { return m_totalDuration; }
    qint64 position() const { return m_currentPosition; }
    bool isPaused() const { return m_isPaused; }
    qreal zoomLevel() const { return m_zoomLevel; }
    qreal rotationAngle() const { return m_rotationAngle; }
    QString currentMediaType() const { return m_currentMediaType; }

signals:
    void durationChanged();
    void positionChanged();
    void pausedChanged();
    void zoomLevelChanged();
    void rotationAngleChanged();
    void currentMediaTypeChanged();

private slots:
    void onTimerFire();

private:
    // Qt 核心组件
    QTimer *m_timer = nullptr;
    QImage m_currentImage;
    QReadWriteLock m_imageLock;  // 读写锁，优化读多写少场景
    QElapsedTimer m_masterClock; // 音画同步主时钟
    
    // 纹理复用优化
    QSGTexture *m_cachedTexture = nullptr;
    QSize m_cachedTextureSize;

    // 多线程组件
    QThread *m_decoderThread = nullptr;
    DecoderWorker *m_decoderWorker = nullptr;

    // 状态变量 - 使用原子变量减少锁竞争
    std::atomic<qint64> m_totalDuration{0};
    std::atomic<qint64> m_currentPosition{0};
    std::atomic<qint64> m_clockOffset{0};
    std::atomic<qint64> m_audioPosition{0}; // 音频时间戳跟踪
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isFirstFrame{true};
    qint64 m_syncThreshold = 40; // 同步阈值(ms)

    // FFmpeg 核心变量
    AVFormatContext *m_formatCtx = nullptr;
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;

    // 解码器类
    AudioDecoder *m_audioDecoder = nullptr;
    VideoDecoder *m_videoDecoder = nullptr;
    ImageDecoder *m_imageDecoder = nullptr;

    // 图片查看相关
    qreal m_zoomLevel = 1.0;
    qreal m_rotationAngle = 0.0;
    QString m_currentMediaType = "none"; // "video", "image", "none"
    QMatrix4x4 m_transformMatrix;

    // 私有方法
    void updateTransformMatrix();
    void renderImage(QSGSimpleTextureNode* node, const QRectF& rect);
};

#endif // VIDEOPLAYER_H
