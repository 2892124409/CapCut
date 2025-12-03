#include "videoplayer_impl.h"
#include <QDebug>

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
    m_usingMemorySource = false;
    m_currentMemoryData.clear();

    bool keepStartPaused = m_startPausedOnOpen;
    qint64 keepReloadTarget = m_reloadTarget.load();
    bool keepReloadPending = m_reloadPending.load();
    qint64 keepPendingSeek = m_pendingSeek.load();
    bool suppressResetSignals = keepReloadPending;
    cleanup(!suppressResetSignals);
    m_startPausedOnOpen = keepStartPaused;
    m_reloadTarget.store(keepReloadPending ? keepReloadTarget : -1);
    m_reloadPending.store(keepReloadPending);
    m_pendingSeek.store(keepReloadPending ? keepPendingSeek : -1);

    // 创建并启动 Demuxer
    m_audioClockTimer.invalidate();
    m_reachedEof = false;
    m_lastFramePts = 0;
    m_pendingSeek.store(-1);
    m_seekTargetMs.store(-1);
    m_audioClockMs.store(-1);
    m_seekGraceActive = false;
    m_seekTimer.invalidate();
    m_currentFilePath = filePath;
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

bool VideoPlayerImpl::loadFromData(const QByteArray &data, const QString &formatHint)
{
    Q_UNUSED(formatHint);

    m_usingMemorySource = true;
    m_currentMemoryData = data;
    m_currentFilePath.clear();

    bool keepStartPaused = m_startPausedOnOpen;
    qint64 keepReloadTarget = m_reloadTarget.load();
    bool keepReloadPending = m_reloadPending.load();
    qint64 keepPendingSeek = m_pendingSeek.load();
    bool suppressResetSignals = keepReloadPending;
    cleanup(!suppressResetSignals);
    m_startPausedOnOpen = keepStartPaused;
    m_reloadTarget.store(keepReloadPending ? keepReloadTarget : -1);
    m_reloadPending.store(keepReloadPending);
    m_pendingSeek.store(keepReloadPending ? keepPendingSeek : -1);

    // 创建并启动 Demuxer（内存数据源）
    m_audioClockTimer.invalidate();
    m_reachedEof = false;
    m_lastFramePts = 0;
    m_pendingSeek.store(-1);
    m_seekTargetMs.store(-1);
    m_audioClockMs.store(-1);
    m_seekGraceActive = false;
    m_seekTimer.invalidate();
    m_demuxer = new Demuxer(this);
    connect(m_demuxer, &Demuxer::opened, this, &VideoPlayerImpl::onDemuxerOpened);
    connect(m_demuxer, &Demuxer::endOfFile, this, &VideoPlayerImpl::onDemuxerEndOfFile);
    connect(m_demuxer, &Demuxer::failedToOpen, this, &VideoPlayerImpl::onDemuxerFailedToOpen);

    m_demuxer->setMemoryBuffer(m_currentMemoryData);
    m_demuxer->start();

    m_isStopped.store(false);
    emit stoppedStateChanged(false);
    return true;
}

void VideoPlayerImpl::play()
{
    if (m_isPaused.load()) {
        m_isPaused.store(false);
        if (m_audioClockMs.load() > 0) {
            m_audioClockTimer.restart();
        }
        
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
        m_audioClockTimer.invalidate();
        m_reachedEof = false;

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
    qDebug() << "VP::seek request pos" << position << "eof?" << m_reachedEof
             << "demuxer running?" << (m_demuxer && m_demuxer->isRunning())
             << "wasPlaying?" << isPlaying()
             << "usingMemorySource?" << m_usingMemorySource;
    if (!m_demuxer || !m_demuxer->isRunning() || m_reachedEof) {
        if (m_usingMemorySource) {
            if (m_currentMemoryData.isEmpty())
                return;
            m_pendingSeek.store(position);
            m_reloadPending.store(true);
            m_reloadTarget.store(position);
            m_seekTargetMs.store(position);
            m_seekGraceActive = true;
            m_startPausedOnOpen = true; // 用户拖动后由他决定何时播放
            loadFromData(m_currentMemoryData);
            return;
        } else {
            if (m_currentFilePath.isEmpty())
                return;
            m_pendingSeek.store(position);
            m_reloadPending.store(true);
            m_reloadTarget.store(position);
            m_seekTargetMs.store(position);
            m_seekGraceActive = true;
            m_startPausedOnOpen = true; // 用户拖动后由他决定何时播放
            load(m_currentFilePath);
            return;
        }
    }

    const bool wasPlaying = isPlaying();

    // 暂停所有组件，避免旧数据继续播放
    m_timer->stop();
    if (m_demuxer)
        m_demuxer->requestPause();
    if (m_audioDecoder)
        m_audioDecoder->requestPause();
    if (m_videoDecoder)
        m_videoDecoder->requestPause();

    // 请求 seek 并刷新缓存
    m_demuxer->requestSeek(position);

    if (m_audioDecoder)
        m_audioDecoder->requestFlush();
    if (m_videoDecoder)
        m_videoDecoder->setDropUntil(position);
    if (m_videoDecoder) {
        m_videoDecoder->requestFlush();
        m_videoDecoder->clearFrameQueue();
    }

    // 重置状态并锁定目标
    m_hasPendingFrame = false;
    m_pendingFrame = VideoFrame{};
    m_reachedEof = false;
    m_lastFramePts = 0;
    m_reloadTarget.store(-1);
    m_seekTargetMs.store(position);
    m_seekGraceActive = true;
    m_seekTimer.restart();
    if (m_audioDecoder)
        m_audioDecoder->setDropUntil(position);

    // 重置时钟并立刻同步 UI
    m_audioClockMs.store(position);
    m_audioClockTimer.restart();
    m_currentPosition = position;
    qDebug() << "VP::seek state reset to" << position;
    emit positionChanged(position);

    // 恢复播放状态
    if (wasPlaying) {
        if (m_demuxer)
            m_demuxer->requestResume();
    if (m_audioDecoder)
        m_audioDecoder->requestResume();
        if (m_videoDecoder)
            m_videoDecoder->requestResume();
        m_isPaused.store(false);
        m_isStopped.store(false);
        m_timer->start(16);
        emit pausedStateChanged(false);
        emit playingStateChanged(true);
        qDebug() << "VP::seek resume playback from" << position;
    } else {
        m_isPaused.store(true);
        emit pausedStateChanged(true);
        emit playingStateChanged(false);
        qDebug() << "VP::seek stay paused at" << position;
    }
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

static qint64 effectiveAudioClock(const std::atomic<qint64> &clockMs, const QElapsedTimer &timer, qint64 totalDuration)
{
    qint64 base = clockMs.load();
    if (base <= 0)
        return base;

    if (timer.isValid()) {
        qint64 elapsed = timer.elapsed();
        if (elapsed < 300) {
            base += elapsed;
        } else {
            return -1; // 音频时钟停滞，允许视频自由前进
        }
    }

    if (totalDuration > 0)
        base = qMin(base, totalDuration);
    return base;
}

QImage VideoPlayerImpl::currentFrame() const
{
    QReadLocker locker(&m_imageLock);
    return m_currentImage;
}

void VideoPlayerImpl::onDemuxerOpened(qint64 duration, int videoStreamIndex, int audioStreamIndex)
{
    m_totalDuration = duration;
    m_videoStreamIndex = videoStreamIndex;
    m_audioStreamIndex = audioStreamIndex;
    emit durationChanged(duration);

    if (videoStreamIndex == -1) {
        emit errorOccurred("未找到可用的视频流");
        cleanup();
        return;
    }

    // 如果有挂起的 seek（例如尾部拖动后重启），先记录并立即请求
    qint64 pending = m_pendingSeek.exchange(-1);
    bool hadPending = m_reloadPending.exchange(false);
    qint64 target = -1;
    if (pending >= 0) {
        target = pending;
    } else {
        qint64 cached = m_reloadTarget.exchange(-1);
        if (hadPending && cached >= 0) {
            target = cached;
        }
    }
    if (target >= 0) {
        m_demuxer->requestSeek(target);
        m_currentPosition = target;
        m_seekTargetMs.store(target);
        m_seekGraceActive = true;
        m_seekTimer.restart();
        m_audioClockMs.store(-1);
        m_audioClockTimer.invalidate();
        if (m_videoDecoder)
            m_videoDecoder->setDropUntil(target);
        if (m_audioDecoder)
            m_audioDecoder->setDropUntil(target);
        emit positionChanged(target);
        qDebug() << "VP::onOpen apply pending seek to" << target;
    }

    // 初始化并启动视频解码器
    m_videoDecoder = new VideoDecoder(this);
    if (m_videoDecoder->init(m_demuxer->formatContext(), videoStreamIndex)) {
        m_videoDecoder->setDemuxer(m_demuxer);
        m_videoDecoder->start();
    } else {
        delete m_videoDecoder;
        m_videoDecoder = nullptr;
        emit errorOccurred("视频解码器初始化失败");
        cleanup();
        return;
    }

    // 初始化并启动音频解码器
    if (audioStreamIndex != -1) {
        m_audioDecoder = new AudioDecoder(this);
        if (m_audioDecoder->init(m_demuxer->formatContext(), audioStreamIndex)) {
            m_audioDecoder->setDemuxer(m_demuxer);
            connect(m_audioDecoder, &AudioDecoder::audioClockUpdated, this, [this](qint64 clockMs) {
                qint64 target = m_seekTargetMs.load();
                constexpr qint64 kAudioTolerance = 20;
                if (target >= 0 && clockMs + kAudioTolerance < target) {
                    qDebug() << "VP::audioClock skip (before target)"
                             << "clock" << clockMs << "target" << target;
                    return; // 忽略落后于目标的音频时钟，防止进度倒退
                }
                m_audioClockMs = clockMs;
                m_audioClockTimer.restart();
                if (target >= 0 && clockMs + kAudioTolerance < target) {
                    return;
                }
                qint64 report = clockMs;
                if (target >= 0 && report < target)
                    report = target;
                qDebug() << "VP::audioClock set" << clockMs << "report" << report << "target" << target;
                m_currentPosition = report;
                emit positionChanged(report);
            });
            m_audioDecoder->start();
        } else {
            delete m_audioDecoder;
            m_audioDecoder = nullptr;
        }
    }

    // 启动或保持暂停
    if (m_startPausedOnOpen) {
        m_isPaused = true;
        m_isStopped = false;
        m_timer->stop();
        if (m_demuxer)
            m_demuxer->requestPause();
        if (m_audioDecoder)
            m_audioDecoder->requestPause();
        if (m_videoDecoder)
            m_videoDecoder->requestPause();
        emit pausedStateChanged(true);
        emit playingStateChanged(false);
    } else {
        m_isPaused = false;
        emit pausedStateChanged(false);
        emit playingStateChanged(true);
        m_timer->start(16); // 约60fps
    }
    m_startPausedOnOpen = false;
}

void VideoPlayerImpl::onDemuxerEndOfFile()
{
    qDebug() << "VideoPlayerImpl: 到达文件末尾，等待帧队列耗尽";
    m_reachedEof = true;
}

void VideoPlayerImpl::onTimerFire()
{
    // 视频模式,从 VideoDecoder 获取帧并对齐音频时钟
    if (!m_videoDecoder)
        return;

    static constexpr qint64 kMaxLeadMs = 50; // 视频超前音频的容忍度
    static constexpr qint64 kMaxLagMs = 60; // 视频落后音频的容忍度
    static constexpr qint64 kTargetTol = 20; // seek 目标容忍
    static constexpr qint64 kSeekGraceMs = 300;

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
        qint64 audioClock = m_audioDecoder ? effectiveAudioClock(m_audioClockMs, m_audioClockTimer, m_totalDuration.load()) : -1;
        qint64 target = m_seekTargetMs.load();
        bool inSeekGrace = target >= 0 && m_seekTimer.isValid() && m_seekTimer.elapsed() < kSeekGraceMs;
        if (target >= 0 && audioClock >= 0) {
            if (audioClock + kTargetTol < target) {
                audioClock = -1; // 音频落后于目标，暂不参与对齐
            } else if (audioClock < target) {
                audioClock = target; // 抬到目标，避免回跳
            }
        }
        if (audioClock < 0 || inSeekGrace) {
            // 不使用音频时钟对齐，直接显示视频
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
            qDebug() << "VP::render early frame pts" << frame.pts << "audio" << audioClock << "target" << target;
            return;
        }
        // 在容忍范围内
        break;
    }

    if (!hasFrame || frame.image.isNull()) {
        if (m_reachedEof && m_videoDecoder && m_videoDecoder->frameQueueSize() == 0) {
            m_timer->stop();
            m_isPaused = true;
            m_isStopped = true;
            qint64 finalPos = m_totalDuration.load() > 0 ? m_totalDuration.load() : m_lastFramePts;
            if (finalPos > m_currentPosition.load()) {
                m_currentPosition = finalPos;
                emit positionChanged(finalPos);
            }
            emit pausedStateChanged(true);
            emit playingStateChanged(false);
            emit stoppedStateChanged(true);
            emit mediaEnded();
        }
        return;
    }

    {
        QWriteLocker locker(&m_imageLock);
        m_currentImage = frame.image;
    }

    m_lastFramePts = frame.pts;
    // 如果有目标 seek，丢弃早于目标的帧
    qint64 seekTarget = m_seekTargetMs.load();
    if (seekTarget >= 0 && frame.pts + 30 < seekTarget) { // 容忍微小偏差
        return;
    }
    if (seekTarget >= 0 && frame.pts >= seekTarget - kTargetTol) {
        if (m_audioClockMs.load() >= seekTarget - kTargetTol) {
            m_seekTargetMs.store(-1);
            m_seekGraceActive = false;
        } else if (m_seekTimer.isValid() && m_seekTimer.elapsed() > 300) {
            qDebug() << "VP::seek grace expired, clearing target" << seekTarget;
            m_seekTargetMs.store(-1);
            m_seekGraceActive = false;
        }
    }

    qint64 audioClock = effectiveAudioClock(m_audioClockMs, m_audioClockTimer, m_totalDuration.load());
    if (seekTarget >= 0) {
        if (audioClock > 0 && audioClock + kTargetTol < seekTarget) {
            audioClock = -1; // 不用音频时钟推进
        } else if (audioClock > 0 && audioClock < seekTarget) {
            audioClock = seekTarget;
        }
    }
    qint64 reportPos = audioClock > 0 ? audioClock : frame.pts;
    if (seekTarget >= 0 && reportPos < seekTarget) {
        reportPos = seekTarget;
    }
    m_currentPosition = reportPos;
    emit positionChanged(reportPos);
    emit frameChanged(frame.image);
}

void VideoPlayerImpl::onDemuxerFailedToOpen(const QString &error)
{
    qDebug() << "VideoPlayerImpl: 文件打开失败:" << error;
    // 清理所有资源并更新状态
    cleanup();
    emit errorOccurred(error);
}

void VideoPlayerImpl::cleanup(bool emitSignals)
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
    if (emitSignals) {
        m_totalDuration = 0;
        m_currentPosition = 0;
        emit positionChanged(0);
        emit durationChanged(0);
        emit frameChanged(QImage());
    }
    m_audioClockMs = 0;
    m_audioClockTimer.invalidate();
    m_isPaused = false;
    m_isStopped = true;
    m_hasPendingFrame = false;
    m_reachedEof = false;
    m_lastFramePts = 0;
    m_startPausedOnOpen = false;
    m_reloadPending.store(false);
    m_reloadTarget.store(-1);
    m_pendingSeek.store(-1);
    m_seekTargetMs.store(-1);
    m_seekTimer.invalidate();
    m_audioClockMs.store(-1);
    m_seekGraceActive = false;
    if (emitSignals) {
        emit pausedStateChanged(false);
        emit playingStateChanged(false);
        emit stoppedStateChanged(true);
    }
}
