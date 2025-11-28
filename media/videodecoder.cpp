#include "videodecoder.h"
#include "demuxer.h"
#include <QDebug>

VideoDecoder::VideoDecoder(QObject *parent) : QThread(parent) {}

VideoDecoder::~VideoDecoder() {
  requestStop();
  wait();
  cleanup();
}

bool VideoDecoder::init(AVFormatContext *formatCtx, int videoStreamIndex) {
  QMutexLocker locker(&m_mutex);

  if (!formatCtx || videoStreamIndex < 0) {
    return false;
  }

  m_streamIndex = videoStreamIndex;
  m_formatCtx = formatCtx;
  AVCodecParameters *vPar = formatCtx->streams[videoStreamIndex]->codecpar;

  // 查找解码器
  const AVCodec *vCodec = avcodec_find_decoder(vPar->codec_id);
  if (!vCodec) {
    qDebug() << "VideoDecoder: 无法找到视频解码器";
    return false;
  }

  // 分配解码器上下文
  AVCodecContext *codecCtx = avcodec_alloc_context3(vCodec);
  if (!codecCtx) {
    qDebug() << "VideoDecoder: 无法分配解码器上下文";
    return false;
  }

  m_codecCtx.reset(codecCtx, "AVCodecContext");
  avcodec_parameters_to_context(m_codecCtx.get(), vPar);

  if (avcodec_open2(m_codecCtx.get(), vCodec, nullptr) < 0) {
    qDebug() << "VideoDecoder: 无法打开视频解码器";
    m_codecCtx.reset(nullptr, "AVCodecContext");
    return false;
  }

  // 分配帧
  AVFrame *frame = av_frame_alloc();
  AVFrame *frameRGB = av_frame_alloc();
  if (!frame || !frameRGB) {
    qDebug() << "VideoDecoder: 无法分配视频帧";
    return false;
  }

  m_frame.reset(frame, "AVFrame");
  m_frameRGB.reset(frameRGB, "AVFrame");

  // 分配 RGB 缓冲区
  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32, m_codecCtx->width,
                                          m_codecCtx->height, 1);
  m_rgbBuffer.reset((uint8_t *)av_malloc(numBytes * sizeof(uint8_t)));
  if (!m_rgbBuffer) {
    qDebug() << "VideoDecoder: 无法分配RGB缓冲区";
    return false;
  }

  av_image_fill_arrays(m_frameRGB->data, m_frameRGB->linesize,
                       m_rgbBuffer.get(), AV_PIX_FMT_RGB32, m_codecCtx->width,
                       m_codecCtx->height, 1);

  // 初始化图像转换器
  SwsContext *swsCtx =
      sws_getContext(m_codecCtx->width, m_codecCtx->height, m_codecCtx->pix_fmt,
                     m_codecCtx->width, m_codecCtx->height, AV_PIX_FMT_RGB32,
                     SWS_LANCZOS, nullptr, nullptr, nullptr);
  if (!swsCtx) {
    qDebug() << "VideoDecoder: 无法创建图像转换器";
    m_rgbBuffer.reset();
    return false;
  }

  m_swsCtx.reset(swsCtx, "SwsContext");

  qDebug() << "VideoDecoder: 初始化成功，尺寸:" << m_codecCtx->width << "x"
           << m_codecCtx->height;
  return true;
}

void VideoDecoder::run() {
  qDebug() << "VideoDecoder: 线程启动";

  while (!m_stopRequested.load()) {
    // 处理刷新请求
    if (m_flushRequested.load()) {
      QMutexLocker locker(&m_mutex);
      if (m_codecCtx)
        avcodec_flush_buffers(m_codecCtx.get());

      // 清空帧队列
      {
        QMutexLocker frameLocker(&m_frameMutex);
        m_frameQueue.clear();
        m_frameCondition.wakeAll();
      }

      m_flushRequested.store(false);
      qDebug() << "VideoDecoder: 缓冲区已刷新";
      continue;
    }

    // 处理暂停
    if (m_pauseRequested.load()) {
      QThread::msleep(10);
      continue;
    }

    // 检查帧队列是否已满
    {
      QMutexLocker frameLocker(&m_frameMutex);
      if (m_frameQueue.size() >= MAX_FRAME_QUEUE_SIZE) {
        // 队列满,等待消费
        m_frameCondition.wait(&m_frameMutex, 10);
        continue;
      }
    }

    // 从 Demuxer 获取数据包
    if (!m_demuxer) {
      QThread::msleep(10);
      continue;
    }

    AVPacket *packet = m_demuxer->popVideoPacket();
    if (!packet) {
      if (m_stopRequested.load())
        break;
      continue;
    }

    // 解码数据包
    processPacket(packet);
    av_packet_free(&packet);
  }

  qDebug() << "VideoDecoder: 线程退出";
}

bool VideoDecoder::processPacket(AVPacket *packet) {
  QMutexLocker locker(&m_mutex);

  if (!m_codecCtx || !m_frame || !m_frameRGB || !m_formatCtx) {
    return false;
  }

  if (avcodec_send_packet(m_codecCtx.get(), packet) == 0) {
    if (avcodec_receive_frame(m_codecCtx.get(), m_frame.get()) == 0) {
      // 转换到 RGB 格式
      sws_scale(m_swsCtx.get(), (const uint8_t *const *)m_frame->data,
                m_frame->linesize, 0, m_codecCtx->height, m_frameRGB->data,
                m_frameRGB->linesize);

      // 创建 QImage (深拷贝)
      QImage image(m_frameRGB->data[0], m_codecCtx->width, m_codecCtx->height,
                   m_frameRGB->linesize[0], QImage::Format_RGB32);
      image = image.copy(); // 深拷贝,避免数据被覆盖

      // 计算时间戳
      AVRational time_base = m_formatCtx->streams[m_streamIndex]->time_base;
      qint64 pts = 0;

      if (m_frame->best_effort_timestamp != AV_NOPTS_VALUE) {
        pts = m_frame->best_effort_timestamp * av_q2d(time_base) * 1000.0;
      } else if (m_frame->pts != AV_NOPTS_VALUE) {
        pts = m_frame->pts * av_q2d(time_base) * 1000.0;
      } else if (packet->pts != AV_NOPTS_VALUE) {
        pts = packet->pts * av_q2d(time_base) * 1000.0;
      }

      // 将帧放入队列
      {
        QMutexLocker frameLocker(&m_frameMutex);
        VideoFrame frame;
        frame.image = image;
        frame.pts = pts;
        m_frameQueue.enqueue(frame);
        m_frameCondition.wakeOne();
      }

      emit frameDecoded(image, pts);
      return true;
    }
  }
  return false;
}

bool VideoDecoder::popFrame(VideoFrame &frame) {
  QMutexLocker locker(&m_frameMutex);

  if (m_frameQueue.isEmpty())
    return false;

  frame = m_frameQueue.dequeue();
  m_frameCondition.wakeOne(); // 通知解码线程可以继续
  return true;
}

int VideoDecoder::frameQueueSize() const {
  QMutexLocker locker(&m_frameMutex);
  return m_frameQueue.size();
}

void VideoDecoder::cleanup() {
  QMutexLocker locker(&m_mutex);

  // 清空帧队列
  {
    QMutexLocker frameLocker(&m_frameMutex);
    m_frameQueue.clear();
  }

  m_rgbBuffer.reset();
  m_codecCtx.reset(nullptr, "AVCodecContext");
  m_frame.reset(nullptr, "AVFrame");
  m_frameRGB.reset(nullptr, "AVFrame");
  m_swsCtx.reset(nullptr, "SwsContext");

  m_streamIndex = -1;
  m_formatCtx = nullptr;

  qDebug() << "VideoDecoder: 资源清理完成";
}

void VideoDecoder::requestFlush() { m_flushRequested.store(true); }

void VideoDecoder::requestStop() {
  m_stopRequested.store(true);
  m_frameCondition.wakeAll();
}

void VideoDecoder::requestPause() { m_pauseRequested.store(true); }

void VideoDecoder::requestResume() { m_pauseRequested.store(false); }

QSize VideoDecoder::getVideoSize() const {
  if (m_codecCtx) {
    return QSize(m_codecCtx->width, m_codecCtx->height);
  }
  return QSize();
}
