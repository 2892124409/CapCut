#include "audioplayer.h"
#include <QDebug>

AudioPlayer::AudioPlayer(QObject *parent) : IMediaPlayer(parent)
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
    m_usingMemorySource = false;
    m_currentMemoryData.clear();

    cleanup();

    m_demuxer = new Demuxer(this);
    connect(m_demuxer, &Demuxer::opened, this, &AudioPlayer::onDemuxerOpened);
    connect(m_demuxer, &Demuxer::endOfFile, this, &AudioPlayer::onDemuxerEndOfFile);
    connect(m_demuxer, &Demuxer::failedToOpen, this, &AudioPlayer::onDemuxerFailedToOpen);

    m_demuxer->setFilePath(filePath);
    m_demuxer->start();

    m_isStopped.store(false);
    emit stoppedStateChanged(false);
    return true;
}

bool AudioPlayer::loadFromData(const QByteArray &data, const QString &formatHint)
{
    Q_UNUSED(formatHint);

    m_usingMemorySource = true;
    m_currentMemoryData = data;

    cleanup();

    m_demuxer = new Demuxer(this);
    connect(m_demuxer, &Demuxer::opened, this, &AudioPlayer::onDemuxerOpened);
    connect(m_demuxer, &Demuxer::endOfFile, this, &AudioPlayer::onDemuxerEndOfFile);
    connect(m_demuxer, &Demuxer::failedToOpen, this, &AudioPlayer::onDemuxerFailedToOpen);

    m_demuxer->setMemoryBuffer(m_currentMemoryData);
    m_demuxer->start();

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
        m_timer->start(40);
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
        m_timer->stop();
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

    m_demuxer->requestSeek(position);
    if (m_audioDecoder) {
        m_audioDecoder->requestFlush();
        m_audioDecoder->setDropUntil(position);
        m_audioDecoder->hardResetOutput(); // 清空输出缓冲，避免旧音频残留
    }
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

void AudioPlayer::onDemuxerOpened(qint64 duration, int videoStreamIndex, int audioStreamIndex)
{
    Q_UNUSED(videoStreamIndex);
    m_totalDuration = duration;
    m_audioStreamIndex = audioStreamIndex;
    emit durationChanged(duration);

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
    } else {
        emit errorOccurred("未找到音频流");
    }

    m_isPaused = false;
    emit pausedStateChanged(false);
    emit playingStateChanged(true);
    m_timer->start(40);
}

void AudioPlayer::onDemuxerEndOfFile()
{
    m_isPaused = true;
    m_isStopped = true;
    m_timer->stop();
    emit pausedStateChanged(true);
    emit playingStateChanged(false);
    emit stoppedStateChanged(true);
    emit mediaEnded();
}

void AudioPlayer::onDemuxerFailedToOpen(const QString &error)
{
    cleanup();
    emit errorOccurred(error);
}

void AudioPlayer::onTimerFire()
{
    // 定时推送当前位置，位置由音频时钟驱动
    qint64 clockMs = m_currentPosition.load();
    emit positionChanged(clockMs);
}

void AudioPlayer::cleanup()
{
    m_timer->stop();

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
