#include "audioplayer.h"
#include <QDebug>

AudioPlayer::AudioPlayer(QObject *parent)
    : IMediaPlayer(parent)
{
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &AudioPlayer::onTimerFire);
}

AudioPlayer::~AudioPlayer()
{
    cleanup();
}

bool AudioPlayer::load(const QString &filePath)
{
    cleanup();

    // 创建并启动 Demuxer
    m_demuxer = new Demuxer(this);
    connect(m_demuxer, &Demuxer::opened, this, &AudioPlayer::onDemuxerOpened);
    connect(m_demuxer, &Demuxer::endOfFile, this, &AudioPlayer::onDemuxerEndOfFile);
    connect(m_demuxer, &Demuxer::failedToOpen, this, &AudioPlayer::onDemuxerFailedToOpen); // NEW: Connect failedToOpen

    m_demuxer->setFilePath(filePath);
    m_demuxer->start(); // Start the demuxer thread, which will perform the actual open

    m_isStopped.store(false);
    emit stoppedStateChanged(false);
    return true;
}

void AudioPlayer::play()
{
    if (m_isPaused.load()) {
        m_isPaused.store(false);
        
        if (m_demuxer)
            m_demuxer->requestResume();
        if (m_audioDecoder)
            m_audioDecoder->requestResume();

        emit pausedStateChanged(false);
        emit playingStateChanged(true);
    }
}

void AudioPlayer::pause()
{
    if (!m_isPaused.load()) {
        m_isPaused.store(true);

        if (m_demuxer)
            m_demuxer->requestPause();
        if (m_audioDecoder)
            m_audioDecoder->requestPause();

        emit pausedStateChanged(true);
        emit playingStateChanged(false);
    }
}

void AudioPlayer::stop()
{
    cleanup();
}

void AudioPlayer::seek(qint64 position)
{
    if (!m_demuxer)
        return;

    // 请求所有组件进行 seek
    m_demuxer->requestSeek(position);

    if (m_audioDecoder)
        m_audioDecoder->requestFlush();

    m_currentPosition = position;
    emit positionChanged(position);
}

void AudioPlayer::setVolume(float volume)
{
    if (m_audioDecoder)
        m_audioDecoder->setVolume(volume);
}

qint64 AudioPlayer::duration() const
{
    return m_totalDuration.load();
}

qint64 AudioPlayer::position() const
{
    return m_currentPosition.load();
}

bool AudioPlayer::isPlaying() const
{
    return !m_isPaused.load() && !m_isStopped.load();
}

bool AudioPlayer::isPaused() const
{
    return m_isPaused.load();
}

bool AudioPlayer::isStopped() const
{
    return m_isStopped.load();
}

QString AudioPlayer::mediaType() const
{
    return "audio";
}

QImage AudioPlayer::currentFrame() const
{
    // 音频播放器没有图像帧
    return QImage();
}

bool AudioPlayer::supportsZoom() const
{
    return false; // 音频不支持缩放
}

bool AudioPlayer::supportsRotation() const
{
    return false; // 音频不支持旋转
}

qreal AudioPlayer::zoomLevel() const
{
    return 1.0; // 音频固定缩放级别
}

qreal AudioPlayer::rotationAngle() const
{
    return 0.0; // 音频固定旋转角度
}

void AudioPlayer::setZoomLevel(qreal zoom)
{
    // 音频不支持缩放
}

void AudioPlayer::setRotationAngle(qreal angle)
{
    // 音频不支持旋转
}

void AudioPlayer::resetTransform()
{
    // 音频不支持变换
}

void AudioPlayer::onDemuxerOpened(qint64 duration, int videoStreamIndex, int audioStreamIndex)
{
    m_totalDuration = duration;
    m_audioStreamIndex = audioStreamIndex;
    emit durationChanged(duration);

    // 只处理音频流
    if (audioStreamIndex != -1) {
        m_audioDecoder = new AudioDecoder(this);
        if (m_audioDecoder->init(m_demuxer->formatContext(), audioStreamIndex)) {
            m_audioDecoder->setDemuxer(m_demuxer);
            connect(m_audioDecoder, &AudioDecoder::audioClockUpdated, this, [this](qint64 clockMs) {
                m_currentPosition = clockMs;
                emit positionChanged(clockMs);
            });
            m_audioDecoder->start();
        } else {
            delete m_audioDecoder;
            m_audioDecoder = nullptr;
        }
    }

    // 启动定时器
    m_isPaused = false;
    emit pausedStateChanged(false);
    emit playingStateChanged(true);
}

void AudioPlayer::onDemuxerEndOfFile()
{
    qDebug() << "AudioPlayer: 播放结束";
    m_isPaused = true;
    emit pausedStateChanged(true);
    emit playingStateChanged(false);
    emit mediaEnded();
}

void AudioPlayer::onTimerFire()
{
    // 定时推送当前位置，位置由音频时钟驱动
    qint64 clockMs = m_currentPosition.load();
    emit positionChanged(clockMs);
}

void AudioPlayer::onDemuxerFailedToOpen(const QString &error)
{
    qDebug() << "AudioPlayer: 文件打开失败:" << error;
    // 清理所有资源并更新状态
    cleanup();
    // 可以在这里发出一个更高级别的错误信号通知 UI
    // emit errorOccurred(error);
}

void AudioPlayer::cleanup()
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

    m_audioStreamIndex = -1;
    m_totalDuration = 0;
    m_currentPosition = 0;
    m_isPaused = false;
    m_isStopped = true;

    emit positionChanged(0);
    emit durationChanged(0);
    emit pausedStateChanged(false);
    emit playingStateChanged(false);
    emit stoppedStateChanged(true);
    emit frameChanged(QImage());
}
