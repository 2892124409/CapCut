#include "videoplayer.h"
#include "audiodecoder.h"
#include "demuxer.h"
#include "imagedecoder.h"
#include "videodecoder.h"
#include <QDebug>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QTransform>


VideoPlayer::VideoPlayer(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);

  m_timer = new QTimer(this);
  m_timer->setTimerType(Qt::PreciseTimer);
  connect(m_timer, &QTimer::timeout, this, &VideoPlayer::onTimerFire);
}

VideoPlayer::~VideoPlayer() {
  stop();

  if (m_cachedTexture) {
    delete m_cachedTexture;
    m_cachedTexture = nullptr;
  }
}

void VideoPlayer::stop() {
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

  if (m_cachedTexture) {
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

void VideoPlayer::play(QString filePath) {
  stop();

  // 创建并启动 Demuxer
  m_demuxer = new Demuxer(this);
  connect(m_demuxer, &Demuxer::opened, this, &VideoPlayer::onDemuxerOpened);
  connect(m_demuxer, &Demuxer::endOfFile, this,
          &VideoPlayer::onDemuxerEndOfFile);

  if (!m_demuxer->open(filePath)) {
    qDebug() << "VideoPlayer: 打开文件失败";
    delete m_demuxer;
    m_demuxer = nullptr;
    return;
  }

  // Demuxer 会通过信号通知打开成功,在 onDemuxerOpened 中启动解码器
}

void VideoPlayer::onDemuxerOpened(qint64 duration, int videoStreamIndex,
                                  int audioStreamIndex) {
  m_totalDuration = duration;
  m_videoStreamIndex = videoStreamIndex;
  m_audioStreamIndex = audioStreamIndex;
  emit durationChanged();

  m_isAudioOnly = (videoStreamIndex == -1 && audioStreamIndex != -1);

  if (m_isAudioOnly) {
    m_currentMediaType = "audio";
    emit currentMediaTypeChanged();
  } else {
    m_currentMediaType = "video";
    emit currentMediaTypeChanged();
  }

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
      m_audioDecoder->start();
    } else {
      delete m_audioDecoder;
      m_audioDecoder = nullptr;
    }
  }

  // 启动 Demuxer 线程
  m_demuxer->start();

  // 启动渲染定时器
  m_isPaused = false;
  emit pausedChanged();
  m_masterClock.start();
  m_timer->start(16); // 约60fps
}

void VideoPlayer::onDemuxerEndOfFile() {
  qDebug() << "VideoPlayer: 播放结束";
  m_timer->stop();
  m_isPaused = true;
  emit pausedChanged();
}

void VideoPlayer::pause() {
  if (!m_isPaused.load()) {
    m_timer->stop();
    m_isPaused = true;

    if (m_demuxer)
      m_demuxer->requestPause();
    if (m_audioDecoder)
      m_audioDecoder->requestPause();
    if (m_videoDecoder)
      m_videoDecoder->requestPause();

    emit pausedChanged();
  }
}

void VideoPlayer::resume() {
  if (m_isPaused.load()) {
    m_isPaused = false;

    if (m_demuxer)
      m_demuxer->requestResume();
    if (m_audioDecoder)
      m_audioDecoder->requestResume();
    if (m_videoDecoder)
      m_videoDecoder->requestResume();

    m_masterClock.restart();
    m_timer->start(16);
    emit pausedChanged();
  }
}

void VideoPlayer::seek(qint64 ms) {
  if (!m_demuxer)
    return;

  // 请求所有组件进行 seek
  m_demuxer->requestSeek(ms);

  if (m_audioDecoder)
    m_audioDecoder->requestFlush();
  if (m_videoDecoder)
    m_videoDecoder->requestFlush();

  m_currentPosition = ms;
  emit positionChanged();
  m_masterClock.restart();
}

void VideoPlayer::onTimerFire() {
  if (m_isAudioOnly) {
    // 纯音频模式,根据时钟更新位置
    qint64 elapsed = m_masterClock.elapsed();
    if (elapsed < m_totalDuration.load()) {
      m_currentPosition = elapsed;
      emit positionChanged();
    }
    return;
  }

  // 视频模式,从 VideoDecoder 获取帧
  if (m_videoDecoder) {
    VideoFrame frame;
    if (m_videoDecoder->popFrame(frame)) {
      {
        QWriteLocker locker(&m_imageLock);
        m_currentImage = frame.image;
        m_currentPosition = frame.pts;
      }
      emit positionChanged();
      update();
    }
  }
}

void VideoPlayer::setVolume(float volume) {
  if (m_audioDecoder)
    m_audioDecoder->setVolume(volume);
}

QSGNode *VideoPlayer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
  if (m_isAudioOnly) {
    if (oldNode)
      delete oldNode;
    return nullptr;
  }

  QImage img;
  QSize imgSize;
  {
    QReadLocker locker(&m_imageLock);
    if (m_currentImage.isNull()) {
      if (oldNode)
        delete oldNode;
      return nullptr;
    }
    img = m_currentImage;
    imgSize = img.size();
  }

  QSGSimpleTextureNode *node = static_cast<QSGSimpleTextureNode *>(oldNode);
  bool needNewTexture = false;

  if (!node || !m_cachedTexture || m_cachedTextureSize != imgSize) {
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
      m_cachedTextureSize = imgSize;
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
    if (imgSize.isEmpty())
      node->setRect(rect);
    else {
      qreal scale = qMin(rect.width() / imgSize.width(),
                         rect.height() / imgSize.height());
      qreal w = qRound(imgSize.width() * scale);
      qreal h = qRound(imgSize.height() * scale);
      qreal x = qRound((rect.width() - w) / 2);
      qreal y = qRound((rect.height() - h) / 2);
      node->setRect(x, y, w, h);
    }
  } else {
    if (node)
      delete node;
    return nullptr;
  }

  return node;
}

void VideoPlayer::loadImage(QString filePath) {
  stop();
  if (filePath.startsWith("file:///"))
    filePath.remove("file:///");

  if (!m_imageDecoder)
    m_imageDecoder = new ImageDecoder(this);

  if (m_imageDecoder->loadImage(filePath)) {
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
  } else {
    qDebug() << "图片加载失败:" << filePath;
  }
}

void VideoPlayer::zoomIn() {
  if (m_currentMediaType == "image") {
    m_zoomLevel = qMin(m_zoomLevel * 1.2, 10.0);
    updateTransformMatrix();
    emit zoomLevelChanged();
    update();
  }
}

void VideoPlayer::zoomOut() {
  if (m_currentMediaType == "image") {
    m_zoomLevel = qMax(m_zoomLevel / 1.2, 0.1);
    updateTransformMatrix();
    emit zoomLevelChanged();
    update();
  }
}

void VideoPlayer::resetZoom() {
  if (m_currentMediaType == "image") {
    m_zoomLevel = 1.0;
    updateTransformMatrix();
    emit zoomLevelChanged();
    update();
  }
}

void VideoPlayer::rotateLeft() {
  if (m_currentMediaType == "image") {
    m_rotationAngle -= 90.0;
    if (m_rotationAngle < 0)
      m_rotationAngle += 360.0;
    updateTransformMatrix();
    emit rotationAngleChanged();
    update();
  }
}

void VideoPlayer::rotateRight() {
  if (m_currentMediaType == "image") {
    m_rotationAngle += 90.0;
    if (m_rotationAngle >= 360.0)
      m_rotationAngle -= 360.0;
    updateTransformMatrix();
    emit rotationAngleChanged();
    update();
  }
}

void VideoPlayer::updateTransformMatrix() {
  m_transformMatrix.setToIdentity();
  if (m_currentMediaType == "image") {
    m_transformMatrix.scale(m_zoomLevel, m_zoomLevel);
    m_transformMatrix.rotate(m_rotationAngle, 0, 0, 1);
  }
}

void VideoPlayer::renderImage(QSGSimpleTextureNode *node, const QRectF &rect) {
  // 保留用于未来扩展
}