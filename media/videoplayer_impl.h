#ifndef VIDEOPLAYER_IMPL_H
#define VIDEOPLAYER_IMPL_H

#include "imediaplayer.h"
#include "demuxer.h"
#include "audiodecoder.h"
#include "videodecoder.h"
#include <QReadWriteLock>
#include <QTimer>
#include <QElapsedTimer>
#include <QString>
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
    QImage currentFrame() const override;

private slots:
      void onDemuxerOpened(qint64 duration, int videoStreamIndex, int audioStreamIndex);
      void onDemuxerEndOfFile();
      void onDemuxerFailedToOpen(const QString &error); // NEW: Slot for demuxer open failure
      void onTimerFire();
private:
    void cleanup(bool emitSignals = true);

    // Qt 核心组件
    QTimer *m_timer = nullptr;
    QImage m_currentImage;
    mutable QReadWriteLock m_imageLock;

    // 多线程组件
    Demuxer *m_demuxer = nullptr;
    AudioDecoder *m_audioDecoder = nullptr;
    VideoDecoder *m_videoDecoder = nullptr;

    // 状态变量
    std::atomic<qint64> m_totalDuration{0};
    std::atomic<qint64> m_currentPosition{0};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isStopped{true};
    bool m_reachedEof = false;
    QString m_currentFilePath;
    bool m_startPausedOnOpen = false;
    std::atomic<qint64> m_reloadTarget{ -1 };
    std::atomic<qint64> m_seekTargetMs{ -1 };
    QElapsedTimer m_seekTimer;
    bool m_seekGraceActive = false;

    // 流索引
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;

    // 纯音频播放相关
    std::atomic<qint64> m_audioClockMs{0};
    QElapsedTimer m_audioClockTimer;

    // 提前到达的帧缓存，等音频时钟追上再显示
    VideoFrame m_pendingFrame;
    bool m_hasPendingFrame = false;
    qint64 m_lastFramePts = 0;
    std::atomic<qint64> m_pendingSeek{-1};
    std::atomic<bool> m_reloadPending{false};
};

#endif // VIDEOPLAYER_IMPL_H
