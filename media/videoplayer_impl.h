#ifndef VIDEOPLAYER_IMPL_H
#define VIDEOPLAYER_IMPL_H

#include "imediaplayer.h"
#include "demuxer.h"
#include "audiodecoder.h"
#include "videodecoder.h"
#include <QQuickItem>
#include <QReadWriteLock>
#include <QElapsedTimer>
#include <QTimer>
#include <atomic>

/**
 * @brief 视频播放器实现
 * 
 * 基于原有VideoPlayer重构，实现IMediaPlayer接口
 */
class VideoPlayerImpl : public IMediaPlayer {
    Q_OBJECT

public:
    explicit VideoPlayerImpl(QObject *parent = nullptr);
    ~VideoPlayerImpl() override;

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

private slots:
      void onDemuxerOpened(qint64 duration, int videoStreamIndex, int audioStreamIndex);
      void onDemuxerEndOfFile();
      void onDemuxerFailedToOpen(const QString &error); // NEW: Slot for demuxer open failure
      void onTimerFire();
private:
    // 私有方法
    void updateTransformMatrix();
    void cleanup();

    // Qt 核心组件
    QTimer *m_timer = nullptr;
    QImage m_currentImage;
    mutable QReadWriteLock m_imageLock;
    QElapsedTimer m_masterClock;

    // 多线程组件
    Demuxer *m_demuxer = nullptr;
    AudioDecoder *m_audioDecoder = nullptr;
    VideoDecoder *m_videoDecoder = nullptr;

    // 状态变量
    std::atomic<qint64> m_totalDuration{0};
    std::atomic<qint64> m_currentPosition{0};
    std::atomic<qint64> m_clockOffset{0};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isStopped{true};

    // 流索引
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;

    // 纯音频播放相关
    bool m_isAudioOnly = false;
};

#endif // VIDEOPLAYER_IMPL_H
