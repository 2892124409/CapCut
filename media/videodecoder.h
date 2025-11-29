#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include "ffmpeg_resource_manager.h"
#include <QImage>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>
#include <atomic>

// 前向声明
class Demuxer;

// 视频帧结构
struct VideoFrame {
  QImage image;
  qint64 pts;
};

class VideoDecoder : public QThread {
  Q_OBJECT

public:
  explicit VideoDecoder(QObject *parent = nullptr);
  ~VideoDecoder() override;

  // 初始化视频解码器
  bool init(AVFormatContext *formatCtx, int videoStreamIndex);

  // 获取视频尺寸
  QSize getVideoSize() const;

  // 设置 Demuxer 引用
  void setDemuxer(Demuxer *demuxer) { m_demuxer = demuxer; }

  // 从帧队列获取帧 (供主线程调用)
  bool popFrame(VideoFrame &frame);
  int frameQueueSize() const;
  void clearFrameQueue();

  // 控制操作
  void requestFlush();
  void requestStop();
  void requestPause();
  void requestResume();
  void setDropUntil(qint64 ms) { m_dropUntilMs.store(ms); }

  // 清理资源
  void cleanup();

signals:
  void frameDecoded(const QImage &image, qint64 pts);

protected:
  void run() override;

private:
  // 处理单个数据包
  bool processPacket(AVPacket *packet);

  // FFmpeg 视频相关变量
  AVFormatContext *m_formatCtx = nullptr;
  FFmpeg::TrackedAVCodecContext m_codecCtx;
  FFmpeg::TrackedAVFrame m_frame;
  FFmpeg::TrackedAVFrame m_frameRGB;
  FFmpeg::TrackedSwsContext m_swsCtx;
  int m_streamIndex = -1;

  // RGB缓冲区
  std::unique_ptr<uint8_t, decltype(&av_free)> m_rgbBuffer{nullptr, &av_free};

  // 帧队列
  QQueue<VideoFrame> m_frameQueue;
  mutable QMutex m_frameMutex;
  QWaitCondition m_frameCondition;
  static constexpr int MAX_FRAME_QUEUE_SIZE = 10;

  // Demuxer 引用
  Demuxer *m_demuxer = nullptr;

  // 线程控制
  std::atomic<bool> m_stopRequested{false};
  std::atomic<bool> m_pauseRequested{false};
  std::atomic<bool> m_flushRequested{false};
  std::atomic<qint64> m_dropUntilMs{-1};

  // 同步控制
  QMutex m_mutex;
};

#endif // VIDEODECODER_H
