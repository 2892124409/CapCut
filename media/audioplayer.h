#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include "imediaplayer.h"
#include "demuxer.h"
#include "audiodecoder.h"
#include <QElapsedTimer>
#include <QTimer>
#include <atomic>

/**
 * @brief 音频播放器实现
 * 
 * 基于原有AudioDecoder重构，实现IMediaPlayer接口
 */
class AudioPlayer : public IMediaPlayer {
    Q_OBJECT

public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

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
    void cleanup();

    // Qt 核心组件
    QTimer *m_timer = nullptr;
    QElapsedTimer m_masterClock;

    // 多线程组件
    Demuxer *m_demuxer = nullptr;
    AudioDecoder *m_audioDecoder = nullptr;

    // 状态变量
    std::atomic<qint64> m_totalDuration{0};
    std::atomic<qint64> m_currentPosition{0};
    std::atomic<qint64> m_clockOffset{0};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isStopped{true};

    // 流索引
    int m_audioStreamIndex = -1;
};

#endif // AUDIOPLAYER_H
