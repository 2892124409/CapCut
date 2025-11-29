#include "demuxer.h"
#include <QDebug>

Demuxer::Demuxer(QObject *parent) : QThread(parent) {}

Demuxer::~Demuxer() {
  requestStop();
  wait();

  if (m_formatCtx) {
    avformat_close_input(&m_formatCtx);
    m_formatCtx = nullptr;
  }

  clearQueues();
}

void Demuxer::setFilePath(const QString &filePath) {
  m_filePath = filePath;
}

bool Demuxer::_performOpenBlocking() {
  QString path = m_filePath;
  if (path.startsWith("file:///"))
    path.remove("file:///");

  m_formatCtx = avformat_alloc_context();
  if (avformat_open_input(&m_formatCtx, path.toStdString().c_str(), nullptr,
                          nullptr) != 0) {
    qDebug() << "Demuxer: 无法打开文件" << path;
    emit failedToOpen("无法打开文件");
    return false;
  }

  m_formatCtx->probesize = 1024 * 1024;
  m_formatCtx->max_analyze_duration = 100000;

  if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
    qDebug() << "Demuxer: 无法获取流信息";
    emit failedToOpen("无法获取流信息");
    avformat_close_input(&m_formatCtx);
    m_formatCtx = nullptr;
    return false;
  }

  m_duration = m_formatCtx->duration * 1000 / AV_TIME_BASE;

  // 查找视频流和音频流
  for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
    AVStream *stream = m_formatCtx->streams[i];
    if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      if (m_videoStreamIndex == -1)
        m_videoStreamIndex = i;
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      if (m_audioStreamIndex == -1)
        m_audioStreamIndex = i;
    }
  }

  qDebug() << "Demuxer: 文件打开成功, 视频流:" << m_videoStreamIndex
           << "音频流:" << m_audioStreamIndex << "时长:" << m_duration << "ms";

  emit opened(m_duration, m_videoStreamIndex, m_audioStreamIndex);
  return true;
}

void Demuxer::run() {
  qDebug() << "Demuxer: 线程启动";

  if (!m_filePath.isEmpty()) { // 仅当设置了文件路径时尝试打开
      if (!_performOpenBlocking()) {
          qDebug() << "Demuxer: 文件打开失败，线程退出。";
          // failedToOpen 信号已在 _performOpenBlocking 中发出
          // 在这里发出 endOfFile 信号，以便清理或通知播放器
          emit endOfFile();
          return; // 打开失败，退出线程
      }
  } else {
      qDebug() << "Demuxer: 没有设置文件路径，线程退出。";
      emit failedToOpen("没有设置文件路径");
      return; // 没有文件路径，退出线程
  }

  while (!m_stopRequested.load()) {
    // 处理 seek 请求
    if (m_seekRequested.load()) {
      qint64 targetMs = m_seekTarget.load();
      qDebug() << "Demuxer: 执行 seek 到" << targetMs << "ms";

      // 清空队列
      clearQueues();

      // 执行 seek
      int64_t targetTs = targetMs * AV_TIME_BASE / 1000;
      av_seek_frame(m_formatCtx, -1, targetTs, AVSEEK_FLAG_BACKWARD);

      m_seekRequested.store(false);
    }

    // 处理暂停
    if (m_pauseRequested.load()) {
      QThread::msleep(10);
      continue;
    }

    // 检查队列是否已满
    bool audioFull =
        (m_audioStreamIndex >= 0 && audioQueueSize() >= MAX_QUEUE_SIZE);
    bool videoFull =
        (m_videoStreamIndex >= 0 && videoQueueSize() >= MAX_QUEUE_SIZE);

    if (audioFull || videoFull) {
      QThread::msleep(10);
      continue;
    }

    // 读取数据包
    AVPacket *packet = av_packet_alloc();
    int ret = av_read_frame(m_formatCtx, packet);

    if (ret < 0) {
      av_packet_free(&packet);
      if (ret == AVERROR_EOF) {
        qDebug() << "Demuxer: 到达文件末尾";
        emit endOfFile();
      }
      break;
    }

    // 分发数据包到对应队列
    if (packet->stream_index == m_audioStreamIndex) {
      pushAudioPacket(packet);
    } else if (packet->stream_index == m_videoStreamIndex) {
      pushVideoPacket(packet);
    } else {
      av_packet_free(&packet);
    }
  }

  qDebug() << "Demuxer: 线程退出";
}

void Demuxer::clearQueues() {
  // 清空音频队列
  {
    QMutexLocker locker(&m_audioMutex);
    while (!m_audioQueue.isEmpty()) {
      AVPacket *packet = m_audioQueue.dequeue();
      av_packet_free(&packet);
    }
    m_audioCondition.wakeAll();
  }

  // 清空视频队列
  {
    QMutexLocker locker(&m_videoMutex);
    while (!m_videoQueue.isEmpty()) {
      AVPacket *packet = m_videoQueue.dequeue();
      av_packet_free(&packet);
    }
    m_videoCondition.wakeAll();
  }
}

void Demuxer::pushAudioPacket(AVPacket *packet) {
  QMutexLocker locker(&m_audioMutex);
  m_audioQueue.enqueue(packet);
  m_audioCondition.wakeOne();
}

void Demuxer::pushVideoPacket(AVPacket *packet) {
  QMutexLocker locker(&m_videoMutex);
  m_videoQueue.enqueue(packet);
  m_videoCondition.wakeOne();
}

AVPacket *Demuxer::popAudioPacket() {
  QMutexLocker locker(&m_audioMutex);

  // 等待队列有数据或停止信号
  while (m_audioQueue.isEmpty() && !m_stopRequested.load()) {
    m_audioCondition.wait(&m_audioMutex, 100);
  }

  if (m_stopRequested.load() || m_audioQueue.isEmpty())
    return nullptr;

  return m_audioQueue.dequeue();
}

AVPacket *Demuxer::popVideoPacket() {
  QMutexLocker locker(&m_videoMutex);

  // 等待队列有数据或停止信号
  while (m_videoQueue.isEmpty() && !m_stopRequested.load()) {
    m_videoCondition.wait(&m_videoMutex, 100);
  }

  if (m_stopRequested.load() || m_videoQueue.isEmpty())
    return nullptr;

  return m_videoQueue.dequeue();
}

int Demuxer::audioQueueSize() const {
  QMutexLocker locker(&m_audioMutex);
  return m_audioQueue.size();
}

int Demuxer::videoQueueSize() const {
  QMutexLocker locker(&m_videoMutex);
  return m_videoQueue.size();
}

void Demuxer::requestSeek(qint64 ms) {
  m_seekTarget.store(ms);
  m_seekRequested.store(true);
}

void Demuxer::requestStop() {
  m_stopRequested.store(true);
  m_audioCondition.wakeAll();
  m_videoCondition.wakeAll();
}

void Demuxer::requestPause() { m_pauseRequested.store(true); }

void Demuxer::requestResume() { m_pauseRequested.store(false); }
