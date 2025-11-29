#include "mediacontroller.h"
#include "videoplayer_impl.h"
#include "imageviewer.h"
#include "audioplayer.h"
#include <QDebug>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QFileInfo>

MediaController::MediaController(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    
    // 创建渲染定时器
    m_renderTimer = new QTimer(this);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
    connect(m_renderTimer, &QTimer::timeout, this, [this]() {
        update();
    });
    m_renderTimer->start(33); // 约30fps
}

MediaController::~MediaController()
{
    cleanupCurrentPlayer();
    
    if (m_cachedTexture) {
        delete m_cachedTexture;
        m_cachedTexture = nullptr;
    }
}

bool MediaController::loadMedia(const QString &filePath)
{
    cleanupCurrentPlayer();
    
    m_currentPlayer = createMediaPlayer(filePath);
    if (!m_currentPlayer) {
        emit errorOccurred("无法创建媒体播放器");
        return false;
    }
    
    // 连接信号
    connect(m_currentPlayer, &IMediaPlayer::frameChanged, this, &MediaController::onFrameChanged);
    connect(m_currentPlayer, &IMediaPlayer::durationChanged, this, &MediaController::onDurationChanged);
    connect(m_currentPlayer, &IMediaPlayer::positionChanged, this, &MediaController::onPositionChanged);
    connect(m_currentPlayer, &IMediaPlayer::playingStateChanged, this, &MediaController::onPlayingStateChanged);
    connect(m_currentPlayer, &IMediaPlayer::pausedStateChanged, this, &MediaController::onPausedStateChanged);
    connect(m_currentPlayer, &IMediaPlayer::stoppedStateChanged, this, &MediaController::onStoppedStateChanged);
    connect(m_currentPlayer, &IMediaPlayer::zoomLevelChanged, this, &MediaController::onZoomLevelChanged);
    connect(m_currentPlayer, &IMediaPlayer::rotationAngleChanged, this, &MediaController::onRotationAngleChanged);
    connect(m_currentPlayer, &IMediaPlayer::mediaEnded, this, &MediaController::onMediaEnded);
    connect(m_currentPlayer, &IMediaPlayer::errorOccurred, this, &MediaController::onErrorOccurred);
    
    bool success = m_currentPlayer->load(filePath);
    if (!success) {
        emit errorOccurred("加载媒体文件失败");
        cleanupCurrentPlayer();
    }
    
    return success;
}

void MediaController::play()
{
    if (m_currentPlayer) {
        m_currentPlayer->play();
    }
}

void MediaController::pause()
{
    if (m_currentPlayer) {
        m_currentPlayer->pause();
    }
}

void MediaController::stop()
{
    if (m_currentPlayer) {
        m_currentPlayer->stop();
    }
}

void MediaController::seek(qint64 position)
{
    if (m_currentPlayer) {
        m_currentPlayer->seek(position);
    }
}

void MediaController::setVolume(float volume)
{
    if (m_currentPlayer) {
        m_currentPlayer->setVolume(volume);
    }
}

void MediaController::zoomIn()
{
    if (m_currentPlayer && m_currentPlayer->supportsZoom()) {
        qreal currentZoom = m_currentPlayer->zoomLevel();
        m_currentPlayer->setZoomLevel(qMin(currentZoom * 1.2, 10.0));
    }
}

void MediaController::zoomOut()
{
    if (m_currentPlayer && m_currentPlayer->supportsZoom()) {
        qreal currentZoom = m_currentPlayer->zoomLevel();
        m_currentPlayer->setZoomLevel(qMax(currentZoom / 1.2, 0.1));
    }
}

void MediaController::resetZoom()
{
    if (m_currentPlayer && m_currentPlayer->supportsZoom()) {
        m_currentPlayer->resetTransform();
    }
}

void MediaController::rotateLeft()
{
    if (m_currentPlayer && m_currentPlayer->supportsRotation()) {
        qreal currentAngle = m_currentPlayer->rotationAngle();
        m_currentPlayer->setRotationAngle(currentAngle - 90.0);
    }
}

void MediaController::rotateRight()
{
    if (m_currentPlayer && m_currentPlayer->supportsRotation()) {
        qreal currentAngle = m_currentPlayer->rotationAngle();
        m_currentPlayer->setRotationAngle(currentAngle + 90.0);
    }
}

// 属性读取器实现
qint64 MediaController::duration() const { return m_cachedDuration.load(); }
qint64 MediaController::position() const { return m_cachedPosition.load(); }
bool MediaController::isPlaying() const { return m_cachedPlaying.load(); }
bool MediaController::isPaused() const { return m_cachedPaused.load(); }
bool MediaController::isStopped() const { return m_cachedStopped.load(); }
QString MediaController::mediaType() const { return m_cachedMediaType; }
qreal MediaController::zoomLevel() const { return m_cachedZoomLevel; }
qreal MediaController::rotationAngle() const { return m_cachedRotationAngle; }
bool MediaController::supportsZoom() const { return m_cachedSupportsZoom; }
bool MediaController::supportsRotation() const { return m_cachedSupportsRotation; }

// 槽函数实现
void MediaController::onFrameChanged(const QImage &frame)
{
    QWriteLocker locker(&m_frameLock);
    m_currentFrame = frame;
    update();
}

void MediaController::onDurationChanged(qint64 duration)
{
    m_cachedDuration.store(duration);
    emit durationChanged();
}

void MediaController::onPositionChanged(qint64 position)
{
    m_cachedPosition.store(position);
    emit positionChanged();
}

void MediaController::onPlayingStateChanged(bool playing)
{
    m_cachedPlaying.store(playing);
    emit playingStateChanged();
}

void MediaController::onPausedStateChanged(bool paused)
{
    m_cachedPaused.store(paused);
    emit pausedStateChanged();
}

void MediaController::onStoppedStateChanged(bool stopped)
{
    m_cachedStopped.store(stopped);
    emit stoppedStateChanged();
}

void MediaController::onZoomLevelChanged(qreal zoom)
{
    m_cachedZoomLevel = zoom;
    emit zoomLevelChanged();
}

void MediaController::onRotationAngleChanged(qreal angle)
{
    m_cachedRotationAngle = angle;
    emit rotationAngleChanged();
}

void MediaController::onMediaEnded()
{
    qDebug() << "MediaController: 媒体播放结束";
    // 可以在这里添加播放结束的处理逻辑
}

void MediaController::onErrorOccurred(const QString &error)
{
    qDebug() << "MediaController: 错误发生:" << error;
    emit errorOccurred(error);
}

// 私有方法实现
IMediaPlayer* MediaController::createMediaPlayer(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();
    
    // 根据文件扩展名创建对应的播放器
    if (extension == "mp4" || extension == "avi" || extension == "mkv" || 
        extension == "mov" || extension == "wmv" || extension == "flv" ||
        extension == "webm" || extension == "m4v" || extension == "3gp" ||
        extension == "ts") {
        return new VideoPlayerImpl(this);
    }
    else if (extension == "jpg" || extension == "jpeg" || extension == "png" ||
             extension == "bmp" || extension == "gif" || extension == "tiff" ||
             extension == "tif" || extension == "webp" || extension == "ico" ||
             extension == "svg") {
        return new ImageViewer(this);
    }
    else if (extension == "mp3" || extension == "wav" || extension == "flac" ||
             extension == "aac" || extension == "ogg" || extension == "m4a" ||
             extension == "wma" || extension == "opus" || extension == "aiff" ||
             extension == "ape") {
        return new AudioPlayer(this);
    }
    
    return nullptr;
}

void MediaController::cleanupCurrentPlayer()
{
    if (m_currentPlayer) {
        m_currentPlayer->stop();
        m_currentPlayer->deleteLater();
        m_currentPlayer = nullptr;
    }
    
    // 重置状态
    m_cachedDuration.store(0);
    m_cachedPosition.store(0);
    m_cachedPlaying.store(false);
    m_cachedPaused.store(false);
    m_cachedStopped.store(true);
    m_cachedMediaType.clear();
    m_cachedZoomLevel = 1.0;
    m_cachedRotationAngle = 0.0;
    m_cachedSupportsZoom = false;
    m_cachedSupportsRotation = false;
    
    {
        QWriteLocker locker(&m_frameLock);
        m_currentFrame = QImage();
    }
    
    emit durationChanged();
    emit positionChanged();
    emit playingStateChanged();
    emit pausedStateChanged();
    emit stoppedStateChanged();
    emit mediaTypeChanged();
    emit zoomLevelChanged();
    emit rotationAngleChanged();
    emit supportsZoomChanged();
    emit supportsRotationChanged();
}

QSGNode* MediaController::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QImage img;
    {
        QReadLocker locker(&m_frameLock);
        if (m_currentFrame.isNull()) {
            if (oldNode) {
                delete oldNode;
            }
            return nullptr;
        }
        img = m_currentFrame;
    }
    
    QSGSimpleTextureNode *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    bool needNewTexture = false;
    
    if (!node || !m_cachedTexture || m_cachedTextureSize != img.size()) {
        needNewTexture = true;
        if (m_cachedTexture) {
            delete m_cachedTexture;
            m_cachedTexture = nullptr;
        }
        if (node) {
            delete node;
            node = nullptr;
        }
    }
    
    if (needNewTexture) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(false);
        m_cachedTexture = window()->createTextureFromImage(img);
        if (m_cachedTexture) {
            m_cachedTexture->setFiltering(QSGTexture::Linear);
            m_cachedTexture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            m_cachedTexture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            m_cachedTextureSize = img.size();
            node->setTexture(m_cachedTexture);
        } else {
            delete node;
            return nullptr;
        }
    } else {
        QSGTexture *newTexture = window()->createTextureFromImage(img);
        if (newTexture) {
            newTexture->setFiltering(QSGTexture::Linear);
            newTexture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            newTexture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            delete m_cachedTexture;
            m_cachedTexture = newTexture;
            node->setTexture(m_cachedTexture);
        }
    }
    
    if (node && m_cachedTexture) {
        QRectF rect = boundingRect();
        if (img.size().isEmpty()) {
            node->setRect(rect);
        } else {
            qreal scale = qMin(rect.width() / img.width(), rect.height() / img.height());
            qreal w = qRound(img.width() * scale);
            qreal h = qRound(img.height() * scale);
            qreal x = qRound((rect.width() - w) / 2);
            qreal y = qRound((rect.height() - h) / 2);
            node->setRect(x, y, w, h);
        }
    } else {
        if (node) {
            delete node;
        }
        return nullptr;
    }
    
    return node;
}
