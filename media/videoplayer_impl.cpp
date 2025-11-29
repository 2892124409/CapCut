#include "videoplayer_impl.h"
#include <QDebug>
#include <QQuickWindow>

VideoPlayerImpl::VideoPlayerImpl(QObject *parent)
    : IMediaPlayer(parent)
{
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &VideoPlayerImpl::onTimerFire);
}

VideoPlayerImpl::~VideoPlayerImpl()
{
    cleanup();
}

bool VideoPlayerImpl::load(const QString &filePath)
{
    cleanup();

    // 创建并启动 Demuxer
    m_demuxer = new Demuxer(this);
    connect(m_demuxer, &Demuxer::opened, this, &VideoPlayerImpl::onDemuxerOpened);
    connect(m_demuxer, &Demuxer::endOfFile, this, &VideoPlayerImpl::onDemuxerEndOfFile);
    connect(m_demuxer, &Demuxer::failedToOpen, this, &VideoPlayerImpl::onDemuxerFailedToOpen); // NEW: Connect failedToOpen

    m_demuxer->setFilePath(filePath);
    m_demuxer->start(); // Start the demuxer thread, which will perform the actual open

    m_isStopped.store(false);
    emit stoppedStateChanged(false);
    return true;
}

void VideoPlayerImpl::play()
{
    if (m_isPaused.load()) {
        m_isPaused.store(false);
        
        if (m_demuxer)
            m_demuxer->requestResume();
        if (m_audioDecoder)
            m_audioDecoder->requestResume();
        if (m_videoDecoder)
            m_videoDecoder->requestResume();

        m_timer->start(16);
        
        emit pausedStateChanged(false);
        emit playingStateChanged(true);
    }
}

void VideoPlayerImpl::pause()
{
    if (!m_isPaused.load()) {
        m_timer->stop();
        m_isPaused.store(true);

        if (m_demuxer)
            m_demuxer->requestPause();
        if (m_audioDecoder)
            m_audioDecoder->requestPause();
        if (m_videoDecoder)
            m_videoDecoder->requestPause();

        emit pausedStateChanged(true);
        emit playingStateChanged(false);
    }
}

void VideoPlayerImpl::stop()
{
    cleanup();
}

void VideoPlayerImpl::seek(qint64 position)
{
    if (!m_demuxer)
        return;

    // 请求所有组件进行 seek
    m_demuxer->requestSeek(position);

    if (m_audioDecoder)
        m_audioDecoder->requestFlush();

    if (m_videoDecoder) {
        m_videoDecoder->requestFlush();
        m_videoDecoder->clearFrameQueue();
    }

    m_currentPosition = position;
    emit positionChanged(position);
}

void VideoPlayerImpl::setVolume(float volume)
{
    if (m_audioDecoder)
        m_audioDecoder->setVolume(volume);
}

qint64 VideoPlayerImpl::duration() const
{
    return m_totalDuration.load();
}

qint64 VideoPlayerImpl::position() const
{
    return m_currentPosition.load();
}

bool VideoPlayerImpl::isPlaying() const
{
    return !m_isPaused.load() && !m_isStopped.load();
}

bool VideoPlayerImpl::isPaused() const
{
    return m_isPaused.load();
}

bool VideoPlayerImpl::isStopped() const
{
    return m_isStopped.load();
}

QString VideoPlayerImpl::mediaType() const
{
    return m_isAudioOnly ? "audio" : "video";
}

QImage VideoPlayerImpl::currentFrame() const
{
    QReadLocker locker(&m_imageLock);
    return m_currentImage;
}

bool VideoPlayerImpl::supportsZoom() const
{
    return false; // 视频不支持缩放
}

bool VideoPlayerImpl::supportsRotation() const
{
    return false; // 视频不支持旋转
}

qreal VideoPlayerImpl::zoomLevel() const
{
    return 1.0; // 视频固定缩放级别
}

qreal VideoPlayerImpl::rotationAngle() const
{
    return 0.0; // 视频固定旋转角度
}

void VideoPlayerImpl::setZoomLevel(qreal zoom)
{
    // 视频不支持缩放
}

void VideoPlayerImpl::setRotationAngle(qreal angle)
{
    // 视频不支持旋转
}

void VideoPlayerImpl::resetTransform()
{
    // 视频不支持变换
}

void VideoPlayerImpl::onDemuxerOpened(qint64 duration, int videoStreamIndex, int audioStreamIndex)
{
    m_totalDuration = duration;
    m_videoStreamIndex = videoStreamIndex;
    m_audioStreamIndex = audioStreamIndex;
    emit durationChanged(duration);

    m_isAudioOnly = (videoStreamIndex == -1 && audioStreamIndex != -1);

    // 初始化并启动视频解码器
    if (videoStreamIndex != -1) {
        m_videoDecoder = new VideoDecoder(this);
        if (m_videoDecoder->init(m_demuxer->formatContext(), videoStreamIndex)) {
            m_videoDecoder->setDemuxer(m_demuxer);
            m_videoDecoder->start();
        } else {
            delete m_videoDecoder;
            m_videoDecoder = nullptr;
        }
    }

    // 初始化并启动音频解码器
    if (audioStreamIndex != -1) {
        m_audioDecoder = new AudioDecoder(this);
        if (m_audioDecoder->init(m_demuxer->formatContext(), audioStreamIndex)) {
            m_audioDecoder->setDemuxer(m_demuxer);
            connect(m_audioDecoder, &AudioDecoder::audioClockUpdated, this, [this](qint64 clockMs) {
                m_audioClockMs = clockMs;
                m_currentPosition = clockMs;
                emit positionChanged(clockMs);
            });
            m_audioDecoder->start();
        } else {
            delete m_audioDecoder;
            m_audioDecoder = nullptr;
        }
    }

    // 启动渲染定时器
    m_isPaused = false;
    emit pausedStateChanged(false);
    emit playingStateChanged(true);
    m_timer->start(16); // 约60fps
}

void VideoPlayerImpl::onDemuxerEndOfFile()
{
    qDebug() << "VideoPlayerImpl: 播放结束";
    m_timer->stop();
    m_isPaused = true;
    emit pausedStateChanged(true);
    emit playingStateChanged(false);
    emit mediaEnded();
}

void VideoPlayerImpl::onTimerFire()
{
    if (m_isAudioOnly) {
        qint64 audioClock = m_audioClockMs.load();
        if (audioClock > 0) {
            m_currentPosition = audioClock;
            emit positionChanged(audioClock);
        }
        return;
    }

    // 视频模式,从 VideoDecoder 获取帧并对齐音频时钟
    if (!m_videoDecoder)
        return;

    static constexpr qint64 kMaxLeadMs = 80; // 视频超前音频的容忍度
    static constexpr qint64 kMaxLagMs = 120; // 视频落后音频的容忍度

    VideoFrame frame;
    auto fetchFrame = [&]() -> bool {
        if (m_hasPendingFrame) {
            frame = m_pendingFrame;
            m_hasPendingFrame = false;
            return true;
        }
        return m_videoDecoder->popFrame(frame);
    };

    bool hasFrame = fetchFrame();
    while (hasFrame) {
        qint64 audioClock = m_audioClockMs.load();
        if (audioClock <= 0) {
            // 没有音频时钟(纯视频或音频尚未输出)，直接显示
            break;
        }

        qint64 delta = frame.pts - audioClock;
        if (delta < -kMaxLagMs) {
            // 视频太晚,丢帧追赶
            hasFrame = fetchFrame();
            continue;
        }

        if (delta > kMaxLeadMs) {
            // 视频太早,缓存这帧等待音频追上
            m_pendingFrame = frame;
            m_hasPendingFrame = true;
            return;
        }
        // 在容忍范围内
        break;
    }

    if (!hasFrame || frame.image.isNull())
        return;

    {
        QWriteLocker locker(&m_imageLock);
        m_currentImage = frame.image;
    }

    qint64 audioClock = m_audioClockMs.load();
    qint64 reportPos = audioClock > 0 ? audioClock : frame.pts;
    m_currentPosition = reportPos;
    emit positionChanged(reportPos);
    emit frameChanged(frame.image);
}

void VideoPlayerImpl::onDemuxerFailedToOpen(const QString &error)
{
    qDebug() << "VideoPlayerImpl: 文件打开失败:" << error;
    // 清理所有资源并更新状态
    cleanup();
    // 可以在这里发出一个更高级别的错误信号通知 UI
    // emit errorOccurred(error);
}

void VideoPlayerImpl::updateTransformMatrix()
{
    // 视频不支持变换
}

void VideoPlayerImpl::cleanup()
{
    m_timer->stop();

    // 停止所有线程
    if (m_demuxer) {
        m_demuxer->requestStop();
        m_demuxer->wait();
        delete m_demuxer;
        m_demuxer = nullptr;
    }

    if (m_audioDecoder) {
        m_audioDecoder->requestStop();
        m_audioDecoder->wait();
        delete m_audioDecoder;
        m_audioDecoder = nullptr;
    }

    if (m_videoDecoder) {
        m_videoDecoder->requestStop();
        m_videoDecoder->wait();
        delete m_videoDecoder;
        m_videoDecoder = nullptr;
    }

    {
        QWriteLocker locker(&m_imageLock);
        m_currentImage = QImage();
    }
    
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_totalDuration = 0;
    m_currentPosition = 0;
    m_audioClockMs = 0;
    m_isAudioOnly = false;
    m_isPaused = false;
    m_isStopped = true;
    m_hasPendingFrame = false;

    emit positionChanged(0);
    emit durationChanged(0);
    emit pausedStateChanged(false);
    emit playingStateChanged(false);
    emit stoppedStateChanged(true);
    emit frameChanged(QImage());
}
