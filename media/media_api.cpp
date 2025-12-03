#include "media_api.h"

#include "audioplayer.h"
#include "imageviewer.h"
#include "imediaplayer.h"
#include "videoplayer_impl.h"

#include <QFileInfo>
#include <QStringList>
#include <utility>

namespace {
bool isVideoExtension(const QString &ext)
{
    static const QStringList kVideoExt = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp", "ts"};
    return kVideoExt.contains(ext);
}

bool isAudioExtension(const QString &ext)
{
    static const QStringList kAudioExt = {"mp3", "wav", "flac", "aac", "ogg", "m4a", "wma", "opus", "aiff", "ape"};
    return kAudioExt.contains(ext);
}

bool isImageExtension(const QString &ext)
{
    static const QStringList kImageExt = {"jpg", "jpeg", "png", "bmp", "gif", "tiff", "tif", "webp", "ico", "svg"};
    return kImageExt.contains(ext);
}

QString fallbackError(const QString &existing, const QString &fallback)
{
    return existing.isEmpty() ? fallback : existing;
}
} // namespace

MediaAPI::MediaAPI(QObject *parent)
    : QObject(parent)
{
}

MediaAPI::~MediaAPI()
{
    cleanupCurrentPlayer();
}

void MediaAPI::connectPlayerSignals(IMediaPlayer *player)
{
    connect(player, &IMediaPlayer::frameChanged, this, &MediaAPI::onFrameChanged);
    connect(player, &IMediaPlayer::durationChanged, this, &MediaAPI::onDurationChanged);
    connect(player, &IMediaPlayer::positionChanged, this, &MediaAPI::onPositionChanged);
    connect(player, &IMediaPlayer::playingStateChanged, this, &MediaAPI::onPlayingStateChanged);
    connect(player, &IMediaPlayer::pausedStateChanged, this, &MediaAPI::onPausedStateChanged);
    connect(player, &IMediaPlayer::stoppedStateChanged, this, &MediaAPI::onStoppedStateChanged);
    connect(player, &IMediaPlayer::mediaEnded, this, &MediaAPI::onMediaEnded);
    connect(player, &IMediaPlayer::errorOccurred, this, &MediaAPI::onErrorOccurred);
}

bool MediaAPI::loadFromPath(const QString &filePath, QString *error)
{
    cleanupCurrentPlayer();
    m_lastError.clear();

    m_player = createMediaPlayer(filePath);
    if (!m_player) {
        m_lastError = QStringLiteral("不支持的媒体类型");
        if (error) {
            *error = m_lastError;
        }
        return false;
    }

    connectPlayerSignals(m_player);

    const bool ok = m_player->load(filePath);
    if (!ok) {
        const QString err = fallbackError(m_lastError, QStringLiteral("加载媒体文件失败"));
        m_lastError = err;
        if (error) {
            *error = err;
        }
        cleanupCurrentPlayer();
        return false;
    }

    return true;
}

bool MediaAPI::loadFromPath(const std::string &filePath, std::string *error)
{
    QString qtError;
    const bool ok = loadFromPath(QString::fromStdString(filePath), error ? &qtError : nullptr);
    if (error) {
        *error = qtError.toStdString();
    }
    return ok;
}

bool MediaAPI::loadVideoFromMemory(const QByteArray &data, const QString &formatHint, QString *error)
{
    cleanupCurrentPlayer();
    m_lastError.clear();

    m_player = new VideoPlayerImpl(this);
    connectPlayerSignals(m_player);

    const bool ok = m_player->loadFromData(data, formatHint);
    if (!ok) {
        const QString err = fallbackError(m_lastError, QStringLiteral("从内存加载视频失败"));
        m_lastError = err;
        if (error) {
            *error = err;
        }
        cleanupCurrentPlayer();
        return false;
    }

    return true;
}

bool MediaAPI::loadAudioFromMemory(const QByteArray &data, const QString &formatHint, QString *error)
{
    Q_UNUSED(formatHint);

    cleanupCurrentPlayer();
    m_lastError.clear();

    m_player = new AudioPlayer(this);
    connectPlayerSignals(m_player);

    const bool ok = m_player->loadFromData(data, QString());
    if (!ok) {
        const QString err = fallbackError(m_lastError, QStringLiteral("从内存加载音频失败"));
        m_lastError = err;
        if (error) {
            *error = err;
        }
        cleanupCurrentPlayer();
        return false;
    }

    return true;
}

bool MediaAPI::loadImageFromMemory(const QByteArray &data, const QString &formatHint, QString *error)
{
    cleanupCurrentPlayer();
    m_lastError.clear();

    m_player = new ImageViewer(this);
    connectPlayerSignals(m_player);

    const bool ok = m_player->loadFromData(data, formatHint);
    if (!ok) {
        const QString err = fallbackError(m_lastError, QStringLiteral("从内存加载图片失败"));
        m_lastError = err;
        if (error) {
            *error = err;
        }
        cleanupCurrentPlayer();
        return false;
    }

    return true;
}

void MediaAPI::play()
{
    if (m_player) {
        m_player->play();
    }
}

void MediaAPI::pause()
{
    if (m_player) {
        m_player->pause();
    }
}

void MediaAPI::stop()
{
    if (m_player) {
        m_player->stop();
    }
}

void MediaAPI::seek(qint64 positionMs)
{
    if (m_player) {
        m_player->seek(positionMs);
    }
}

void MediaAPI::setVolume(float volume)
{
    if (m_player) {
        m_player->setVolume(volume);
    }
}

qint64 MediaAPI::duration() const { return m_cachedDuration.load(); }
qint64 MediaAPI::position() const { return m_cachedPosition.load(); }
bool MediaAPI::isPlaying() const { return m_cachedPlaying.load(); }
bool MediaAPI::isPaused() const { return m_cachedPaused.load(); }
bool MediaAPI::isStopped() const { return m_cachedStopped.load(); }
QString MediaAPI::lastError() const { return m_lastError; }

QImage MediaAPI::currentFrame() const
{
    QReadLocker locker(&m_frameLock);
    return m_lastFrame;
}

void MediaAPI::setErrorCallback(ErrorCallback cb) { m_errorCallback = std::move(cb); }
void MediaAPI::setFrameCallback(FrameCallback cb) { m_frameCallback = std::move(cb); }
void MediaAPI::setPositionCallback(PositionCallback cb) { m_positionCallback = std::move(cb); }
void MediaAPI::setStateCallback(StateCallback cb) { m_stateCallback = std::move(cb); }
void MediaAPI::setEndedCallback(EndedCallback cb) { m_endedCallback = std::move(cb); }

void MediaAPI::onFrameChanged(const QImage &frame)
{
    {
        QWriteLocker locker(&m_frameLock);
        m_lastFrame = frame;
    }
    if (m_frameCallback) {
        m_frameCallback(frame);
    }
}

void MediaAPI::onDurationChanged(qint64 duration)
{
    m_cachedDuration.store(duration);
}

void MediaAPI::onPositionChanged(qint64 position)
{
    m_cachedPosition.store(position);
    if (m_positionCallback) {
        m_positionCallback(position);
    }
}

void MediaAPI::onPlayingStateChanged(bool playing)
{
    m_cachedPlaying.store(playing);
    if (m_stateCallback) {
        m_stateCallback(playing, m_cachedPaused.load(), m_cachedStopped.load());
    }
}

void MediaAPI::onPausedStateChanged(bool paused)
{
    m_cachedPaused.store(paused);
    if (m_stateCallback) {
        m_stateCallback(m_cachedPlaying.load(), paused, m_cachedStopped.load());
    }
}

void MediaAPI::onStoppedStateChanged(bool stopped)
{
    m_cachedStopped.store(stopped);
    if (m_stateCallback) {
        m_stateCallback(m_cachedPlaying.load(), m_cachedPaused.load(), stopped);
    }
}

void MediaAPI::onMediaEnded()
{
    if (m_endedCallback) {
        m_endedCallback();
    }
}

void MediaAPI::onErrorOccurred(const QString &error)
{
    m_lastError = error;
    if (m_errorCallback) {
        m_errorCallback(error);
    }
}

IMediaPlayer *MediaAPI::createMediaPlayer(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    const QString extension = fileInfo.suffix().toLower();

    if (isVideoExtension(extension)) {
        return new VideoPlayerImpl(this);
    }
    if (isAudioExtension(extension)) {
        return new AudioPlayer(this);
    }
    if (isImageExtension(extension)) {
        return new ImageViewer(this);
    }

    return nullptr;
}

void MediaAPI::cleanupCurrentPlayer()
{
    if (m_player) {
        QObject::disconnect(m_player, nullptr, this, nullptr);
        m_player->stop();
        delete m_player;
        m_player = nullptr;
    }

    m_cachedDuration.store(0);
    m_cachedPosition.store(0);
    m_cachedPlaying.store(false);
    m_cachedPaused.store(false);
    m_cachedStopped.store(true);
    m_lastError.clear();

    {
        QWriteLocker locker(&m_frameLock);
        m_lastFrame = QImage();
    }
}
