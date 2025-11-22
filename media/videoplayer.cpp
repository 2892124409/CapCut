#include "videoplayer.h"
#include "audiodecoder.h"
#include "videodecoder.h"
#include "decoderworker.h"
#include "imagedecoder.h"
#include <QDebug>
#include <QSGSimpleTextureNode>
#include <QQuickWindow>
#include <QTransform>

VideoPlayer::VideoPlayer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);

    m_decoderThread = new QThread(this);
    m_decoderWorker = new DecoderWorker();
    m_decoderWorker->moveToThread(m_decoderThread);
    m_decoderThread->start();

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &VideoPlayer::onTimerFire);
}

VideoPlayer::~VideoPlayer()
{
    stop();

    if (m_cachedTexture)
    {
        delete m_cachedTexture;
        m_cachedTexture = nullptr;
    }

    if (m_decoderThread)
    {
        m_decoderThread->quit();
        m_decoderThread->wait();
        delete m_decoderThread;
        m_decoderThread = nullptr;
    }

    if (m_decoderWorker)
    {
        delete m_decoderWorker;
        m_decoderWorker = nullptr;
    }
}

void VideoPlayer::stop()
{
    m_timer->stop();

    if (m_audioDecoder)
    {
        m_audioDecoder->cleanup();
        delete m_audioDecoder;
        m_audioDecoder = nullptr;
    }

    if (m_videoDecoder)
    {
        m_videoDecoder->cleanup();
        delete m_videoDecoder;
        m_videoDecoder = nullptr;
    }

    if (m_formatCtx)
    {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    if (m_cachedTexture)
    {
        delete m_cachedTexture;
        m_cachedTexture = nullptr;
    }
    m_cachedTextureSize = QSize();

    m_currentImage = QImage();
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_totalDuration = 0;
    m_currentPosition = 0;
    m_isAudioOnly = false;
}

void VideoPlayer::play(QString filePath)
{
    stop();
    if (filePath.startsWith("file:///"))
        filePath.remove("file:///");

    m_formatCtx = avformat_alloc_context();
    if (avformat_open_input(&m_formatCtx, filePath.toStdString().c_str(), nullptr, nullptr) != 0)
    {
        qDebug() << "无法打开文件";
        return;
    }

    m_formatCtx->probesize = 1024 * 1024;
    m_formatCtx->max_analyze_duration = 100000;
    avformat_find_stream_info(m_formatCtx, nullptr);

    m_totalDuration = m_formatCtx->duration * 1000 / AV_TIME_BASE;
    emit durationChanged();

    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++)
    {
        AVStream *stream = m_formatCtx->streams[i];
        if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC)
        {
            // qDebug() << "忽略封面图片流";
            continue;
        }
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (m_videoStreamIndex == -1)
                m_videoStreamIndex = i;
        }
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            if (m_audioStreamIndex == -1)
                m_audioStreamIndex = i;
        }
    }

    m_isAudioOnly = (m_videoStreamIndex == -1 && m_audioStreamIndex != -1);

    if (m_isAudioOnly)
    {
        m_currentMediaType = "audio";
        emit currentMediaTypeChanged();
    }
    else
    {
        m_currentMediaType = "video";
        emit currentMediaTypeChanged();
    }

    if (m_videoStreamIndex != -1)
    {
        m_videoDecoder = new VideoDecoder(this);
        if (!m_videoDecoder->init(m_formatCtx, m_videoStreamIndex))
        {
            delete m_videoDecoder;
            m_videoDecoder = nullptr;
        }
    }

    if (m_audioStreamIndex != -1)
    {
        m_audioDecoder = new AudioDecoder(this);
        if (!m_audioDecoder->init(m_formatCtx, m_audioStreamIndex))
        {
            delete m_audioDecoder;
            m_audioDecoder = nullptr;
        }
    }

    if (m_videoStreamIndex != -1 || m_audioStreamIndex != -1)
    {
        m_isFirstFrame = true;
        m_isPaused = false;
        emit pausedChanged();
        m_clockOffset = 0;
        m_masterClock.start();
        m_timer->start(0);
        onTimerFire();
    }
}

void VideoPlayer::pause()
{
    if (m_timer->isActive())
    {
        m_timer->stop();
        bool expected = false;
        if (m_isPaused.compare_exchange_strong(expected, true))
        {
            if (m_audioDecoder)
                m_audioDecoder->pause();
            emit pausedChanged();
        }
    }
}

void VideoPlayer::resume()
{
    bool expected = true;
    if (m_isPaused.compare_exchange_strong(expected, false))
    {
        if (m_audioDecoder)
            m_audioDecoder->resume();
        m_masterClock.restart();
        m_clockOffset.store(m_currentPosition.load());
        m_timer->start(0);
        emit pausedChanged();
    }
}

void VideoPlayer::seek(qint64 ms)
{
    m_timer->stop(); // Seek 期间暂停定时器

    if (!m_formatCtx)
        return;

    int64_t targetTs = ms * AV_TIME_BASE / 1000;
    int ret = av_seek_frame(m_formatCtx, -1, targetTs, AVSEEK_FLAG_BACKWARD);

    if (ret >= 0)
    {
        if (m_videoDecoder)
            m_videoDecoder->flushBuffers();
        if (m_audioDecoder)
            m_audioDecoder->flushBuffers();

        if (m_isAudioOnly)
        {
            m_currentPosition.store(ms);
            emit positionChanged();
        }
        else
        {
            AVPacket *packet = av_packet_alloc();
            bool seekSuccess = false;
            int maxRead = 200;
            while (av_read_frame(m_formatCtx, packet) >= 0 && maxRead-- > 0)
            {
                if (packet->stream_index == m_videoStreamIndex && m_videoDecoder)
                {
                    if (m_videoDecoder->decodePacket(packet))
                    {
                        QImage decodedImage = m_videoDecoder->getCurrentImage();
                        qint64 decodedPts = m_videoDecoder->getCurrentPts();
                        if (decodedPts >= ms)
                        {
                            {
                                QWriteLocker locker(&m_imageLock);
                                m_currentImage = decodedImage;
                                m_currentPosition.store(decodedPts);
                            }
                            update();
                            seekSuccess = true;
                            break;
                        }
                    }
                }
                av_packet_unref(packet);
                if (seekSuccess)
                    break;
            }
            av_packet_free(&packet);
        }

        m_clockOffset.store(ms);
        m_currentPosition.store(ms);
        m_audioPosition.store(ms);
        emit positionChanged();

        m_masterClock.restart();
        // 【核心修复】将 m_isFirstFrame 设为 true
        // 这告诉 onTimerFire：“不要试图追赶 Seek 消耗的时间，从现在开始重新计​​时”
        m_isFirstFrame.store(true);

        if (!m_isPaused.load())
            m_timer->start(0);
    }
    else
    {
        if (!m_isPaused.load())
            m_timer->start(0);
    }
}

void VideoPlayer::onTimerFire()
{
    AVPacket *packet = av_packet_alloc();
    int loopCount = 0;
    bool frameProcessed = false;

    // 流控：防止音频写入阻塞
    if (m_isAudioOnly && m_audioDecoder)
    {
        if (m_audioDecoder->bytesFree() < 16384)
        {
            if (!m_isPaused.load())
                m_timer->start(10);
            av_packet_free(&packet);
            return;
        }
    }

    while (av_read_frame(m_formatCtx, packet) >= 0)
    {
        if (packet->stream_index == m_audioStreamIndex && m_audioDecoder)
        {
            m_audioDecoder->decodePacket(packet);
            av_packet_unref(packet);

            if (m_isAudioOnly)
            {
                if (m_audioDecoder->bytesFree() < 16384)
                {
                    if (!m_isPaused.load())
                        m_timer->start(10);
                    break;
                }
                if (++loopCount > 50)
                {
                    if (!m_isPaused.load())
                        m_timer->start(0);
                    break;
                }
            }
            continue;
        }

        if (packet->stream_index == m_videoStreamIndex && m_videoDecoder)
        {
            if (m_videoDecoder->decodePacket(packet))
            {
                QImage decodedImage = m_videoDecoder->getCurrentImage();
                qint64 decodedPts = m_videoDecoder->getCurrentPts();

                {
                    QWriteLocker locker(&m_imageLock);
                    m_currentImage = decodedImage;
                    m_currentPosition.store(decodedPts);
                }
                emit positionChanged();

                qint64 elapsed = m_masterClock.elapsed() + m_clockOffset.load();
                double diff = decodedPts - elapsed;

                // 如果是首帧（包括Seek后的第一帧），重置时钟
                if (m_isFirstFrame.load())
                {
                    m_isFirstFrame = false;
                    diff = 0;
                    m_masterClock.restart();
                    m_clockOffset = 0; // 注意：这里 offset 其实已经不重要了，因为 diff=0，下一次 elapsed 会很小
                    // 更正：offset 应该保持为当前 PTS，否则进度条会跳回 0
                    m_clockOffset.store(decodedPts);
                }

                update();
                av_packet_unref(packet);
                frameProcessed = true;

                if (m_isPaused.load())
                    break;

                // 正常的同步逻辑
                if (diff > 0)
                    m_timer->start(qMin(diff, 33.0));
                else
                    m_timer->start(0);

                break;
            }
        }
        av_packet_unref(packet);

        if (++loopCount > 100)
        {
            if (!m_isPaused.load())
                m_timer->start(0);
            break;
        }
    }

    if (!frameProcessed && !m_isPaused.load())
    {
        if (m_isAudioOnly)
        {
            qint64 elapsed = m_masterClock.elapsed() + m_clockOffset.load();
            if (elapsed < m_totalDuration.load())
            {
                m_currentPosition.store(elapsed);
                emit positionChanged();
                update();
                m_timer->start(16);
                av_packet_free(&packet);
                return;
            }
        }

        m_timer->stop();
        m_isPaused = true;
        emit pausedChanged();
        if (m_currentPosition.load() < m_totalDuration.load())
        {
            m_currentPosition.store(m_totalDuration.load());
            emit positionChanged();
        }
    }

    av_packet_free(&packet);
}

// 辅助函数保持不变
void VideoPlayer::setVolume(float volume)
{
    if (m_audioDecoder)
        m_audioDecoder->setVolume(volume);
}
QSGNode *VideoPlayer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (m_isAudioOnly)
    {
        if (oldNode)
            delete oldNode;
        return nullptr;
    }
    QImage img;
    QSize imgSize;
    {
        QReadLocker locker(&m_imageLock);
        if (m_currentImage.isNull())
        {
            if (oldNode)
                delete oldNode;
            return nullptr;
        }
        img = m_currentImage;
        imgSize = img.size();
    }
    QSGSimpleTextureNode *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    bool needNewTexture = false;
    if (!node || !m_cachedTexture || m_cachedTextureSize != imgSize)
    {
        needNewTexture = true;
        if (m_cachedTexture)
        {
            delete m_cachedTexture;
            m_cachedTexture = nullptr;
        }
        if (node)
        {
            delete node;
            node = nullptr;
        }
    }
    if (needNewTexture)
    {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(false);
        m_cachedTexture = window()->createTextureFromImage(img);
        if (m_cachedTexture)
        {
            m_cachedTexture->setFiltering(QSGTexture::Linear);
            m_cachedTexture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            m_cachedTexture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            m_cachedTextureSize = imgSize;
            node->setTexture(m_cachedTexture);
        }
        else
        {
            delete node;
            return nullptr;
        }
    }
    else
    {
        QSGTexture *newTexture = window()->createTextureFromImage(img);
        if (newTexture)
        {
            newTexture->setFiltering(QSGTexture::Linear);
            newTexture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            newTexture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            delete m_cachedTexture;
            m_cachedTexture = newTexture;
            node->setTexture(m_cachedTexture);
        }
    }
    if (node && m_cachedTexture)
    {
        QRectF rect = boundingRect();
        if (imgSize.isEmpty())
            node->setRect(rect);
        else
        {
            qreal scale = qMin(rect.width() / imgSize.width(), rect.height() / imgSize.height());
            qreal w = qRound(imgSize.width() * scale);
            qreal h = qRound(imgSize.height() * scale);
            qreal x = qRound((rect.width() - w) / 2);
            qreal y = qRound((rect.height() - h) / 2);
            node->setRect(x, y, w, h);
        }
    }
    else
    {
        if (node)
            delete node;
        return nullptr;
    }
    return node;
}
void VideoPlayer::loadImage(QString filePath)
{
    stop();
    if (filePath.startsWith("file:///"))
        filePath.remove("file:///");
    if (!m_imageDecoder)
        m_imageDecoder = new ImageDecoder(this);
    if (m_imageDecoder->loadImage(filePath))
    {
        QWriteLocker locker(&m_imageLock);
        m_currentImage = m_imageDecoder->getCurrentImage();
        m_currentMediaType = "image";
        m_isAudioOnly = false;
        m_zoomLevel = 1.0;
        m_rotationAngle = 0.0;
        updateTransformMatrix();
        emit currentMediaTypeChanged();
        emit zoomLevelChanged();
        emit rotationAngleChanged();
        update();
        qDebug() << "图片加载成功:" << filePath;
    }
    else
    {
        qDebug() << "图片加载失败:" << filePath;
    }
}
void VideoPlayer::zoomIn()
{
    if (m_currentMediaType == "image")
    {
        m_zoomLevel = qMin(m_zoomLevel * 1.2, 10.0);
        updateTransformMatrix();
        emit zoomLevelChanged();
        update();
    }
}
void VideoPlayer::zoomOut()
{
    if (m_currentMediaType == "image")
    {
        m_zoomLevel = qMax(m_zoomLevel / 1.2, 0.1);
        updateTransformMatrix();
        emit zoomLevelChanged();
        update();
    }
}
void VideoPlayer::resetZoom()
{
    if (m_currentMediaType == "image")
    {
        m_zoomLevel = 1.0;
        updateTransformMatrix();
        emit zoomLevelChanged();
        update();
    }
}
void VideoPlayer::rotateLeft()
{
    if (m_currentMediaType == "image")
    {
        m_rotationAngle -= 90.0;
        if (m_rotationAngle < 0)
            m_rotationAngle += 360.0;
        updateTransformMatrix();
        emit rotationAngleChanged();
        update();
    }
}
void VideoPlayer::rotateRight()
{
    if (m_currentMediaType == "image")
    {
        m_rotationAngle += 90.0;
        if (m_rotationAngle >= 360.0)
            m_rotationAngle -= 360.0;
        updateTransformMatrix();
        emit rotationAngleChanged();
        update();
    }
}
void VideoPlayer::updateTransformMatrix()
{
    m_transformMatrix.setToIdentity();
    if (m_currentMediaType == "image")
    {
        m_transformMatrix.scale(m_zoomLevel, m_zoomLevel);
        m_transformMatrix.rotate(m_rotationAngle, 0, 0, 1);
    }
}
void VideoPlayer::renderImage(QSGSimpleTextureNode *node, const QRectF &rect) {}