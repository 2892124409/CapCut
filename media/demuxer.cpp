#include "demuxer.h"
#include <QDebug>
#include <algorithm>
#include <cstring>

using std::min;

namespace {

// 供 FFmpeg 自定义 IO 使用的读回调（从内存缓冲读取）
static int readPacket(void *opaque, uint8_t *buf, int buf_size)
{
    auto *ctx = reinterpret_cast<Demuxer::MemoryBufferContext *>(opaque);
    if (!ctx || !ctx->data || ctx->size <= 0)
        return AVERROR_EOF;

    int64_t remaining = ctx->size - ctx->pos;
    if (remaining <= 0)
        return AVERROR_EOF;

    int toRead = static_cast<int>(min<int64_t>(buf_size, remaining));
    std::memcpy(buf, ctx->data + ctx->pos, static_cast<size_t>(toRead));
    ctx->pos += toRead;
    return toRead;
}

// 供 FFmpeg 自定义 IO 使用的 seek 回调（支持随机访问和 AVSEEK_SIZE）
static int64_t seek(void *opaque, int64_t offset, int whence)
{
    auto *ctx = reinterpret_cast<Demuxer::MemoryBufferContext *>(opaque);
    if (!ctx)
        return -1;

    if (whence == AVSEEK_SIZE)
        return ctx->size;

    int64_t newPos = -1;
    switch (whence) {
    case SEEK_SET:
        newPos = offset;
        break;
    case SEEK_CUR:
        newPos = ctx->pos + offset;
        break;
    case SEEK_END:
        newPos = ctx->size + offset;
        break;
    default:
        return -1;
    }

    if (newPos < 0 || newPos > ctx->size)
        return -1;

    ctx->pos = newPos;
    return ctx->pos;
}

} // namespace

Demuxer::Demuxer(QObject *parent) : QThread(parent) {}

Demuxer::~Demuxer() {
  requestStop();
  wait();

  if (m_formatCtx) {
    avformat_close_input(&m_formatCtx);
    m_formatCtx = nullptr;
  }

  if (m_avioCtx) {
    if (m_avioCtx->buffer) {
      av_freep(&m_avioCtx->buffer);
    }
    avio_context_free(&m_avioCtx);
    m_avioCtx = nullptr;
  }

  clearQueues();
}

void Demuxer::setFilePath(const QString &filePath) {
  m_filePath = filePath;
  m_sourceType = SourceType::File;
}

void Demuxer::setMemoryBuffer(const QByteArray &buffer)
{
  m_memoryBuffer = buffer;
  m_sourceType = SourceType::Memory;
}

bool Demuxer::_performOpenBlocking() {
  if (m_sourceType == SourceType::File) {
    QString path = m_filePath;
    if (path.startsWith("file:///"))
      path.remove("file:///");

    m_formatCtx = avformat_alloc_context();
    if (!m_formatCtx) {
      emit failedToOpen("无法分配 AVFormatContext");
      return false;
    }

    if (avformat_open_input(&m_formatCtx, path.toStdString().c_str(), nullptr,
                            nullptr) != 0) {
      qDebug() << "Demuxer: 无法打开文件" << path;
      emit failedToOpen("无法打开文件");
      return false;
    }
  } else if (m_sourceType == SourceType::Memory) {
    if (m_memoryBuffer.isEmpty()) {
      emit failedToOpen("内存数据为空");
      return false;
    }

    m_formatCtx = avformat_alloc_context();
    if (!m_formatCtx) {
      emit failedToOpen("无法分配 AVFormatContext");
      return false;
    }

    constexpr int kIoBufferSize = 4 * 1024;
    unsigned char *buffer = static_cast<unsigned char *>(av_malloc(kIoBufferSize));
    if (!buffer) {
      emit failedToOpen("无法分配 AVIO 缓冲区");
      return false;
    }

    m_memCtx.data = reinterpret_cast<const uint8_t *>(m_memoryBuffer.constData());
    m_memCtx.size = m_memoryBuffer.size();
    m_memCtx.pos = 0;

    m_avioCtx = avio_alloc_context(
        buffer,
        kIoBufferSize,
        0,                // 只读
        &m_memCtx,
        &readPacket,
        nullptr,
        &seek);

    if (!m_avioCtx) {
      emit failedToOpen("创建 AVIOContext 失败");
      av_free(buffer);
      return false;
    }

    m_formatCtx->pb = m_avioCtx;
    m_formatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&m_formatCtx, nullptr, nullptr, nullptr) != 0) {
      qDebug() << "Demuxer: 无法从内存打开媒体";
      emit failedToOpen("无法从内存打开媒体数据");

      if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
      }
      if (m_avioCtx) {
        if (m_avioCtx->buffer) {
          av_freep(&m_avioCtx->buffer);
        }
        avio_context_free(&m_avioCtx);
        m_avioCtx = nullptr;
      }
      return false;
    }
  } else {
    qDebug() << "Demuxer: 未设置输入源";
    emit failedToOpen("没有设置文件路径或内存数据源");
    return false;
  }

  // 公共初始化逻辑
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
  m_videoStreamIndex = -1;
  m_audioStreamIndex = -1;
  for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
    AVStream *stream = m_formatCtx->streams[i];
    if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
      continue;
    }
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      if (m_videoStreamIndex == -1)
        m_videoStreamIndex = static_cast<int>(i);
    } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      if (m_audioStreamIndex == -1)
        m_audioStreamIndex = static_cast<int>(i);
    }
  }

  qDebug() << "Demuxer: 输入打开成功, 来源:"
           << (m_sourceType == SourceType::File ? "文件/URL" : "内存")
           << "视频流:" << m_videoStreamIndex
           << "音频流:" << m_audioStreamIndex
           << "时长:" << m_duration << "ms";

  emit opened(m_duration, m_videoStreamIndex, m_audioStreamIndex);
  return true;
}

void Demuxer::run() {
  qDebug() << "Demuxer: 线程启动";

  if (!_performOpenBlocking()) {
      qDebug() << "Demuxer: 打开输入失败，线程退出。";
      // failedToOpen 信号已在 _performOpenBlocking 中发出
      emit endOfFile();
      return; // 打开失败，退出线程
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
