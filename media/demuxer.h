#ifndef DEMUXER_H
#define DEMUXER_H

#include <QMutex>
#include <QQueue>
#include <QString>
#include <QThread>
#include <QWaitCondition>
#include <atomic>


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class Demuxer : public QThread {
  Q_OBJECT

public:
  explicit Demuxer(QObject *parent = nullptr);
  ~Demuxer() override;

  void setFilePath(const QString &filePath);

  // 获取流信息
  int videoStreamIndex() const { return m_videoStreamIndex; }
  int audioStreamIndex() const { return m_audioStreamIndex; }
  AVFormatContext *formatContext() const { return m_formatCtx; }
  qint64 duration() const { return m_duration; }

  // 从队列获取数据包 (供解码器调用)
  AVPacket *popAudioPacket();
  AVPacket *popVideoPacket();

  // 队列状态查询
  int audioQueueSize() const;
  int videoQueueSize() const;

  // 控制操作
  void requestSeek(qint64 ms);
  void requestStop();
  void requestPause();
  void requestResume();

signals:
  void opened(qint64 duration, int videoStreamIndex, int audioStreamIndex);
  void endOfFile();
  void failedToOpen(const QString &error);

protected:
  void run() override;

private:
  // FFmpeg 相关
  AVFormatContext *m_formatCtx = nullptr;
  int m_videoStreamIndex = -1;
  int m_audioStreamIndex = -1;
  qint64 m_duration = 0;
  QString m_filePath; // NEW: Member to store the file path

  // 数据包队列
  QQueue<AVPacket *> m_audioQueue;
  QQueue<AVPacket *> m_videoQueue;

  // 队列大小限制
  static constexpr int MAX_QUEUE_SIZE = 50;

  // 线程同步
  mutable QMutex m_audioMutex;
  mutable QMutex m_videoMutex;
  QWaitCondition m_audioCondition;
  QWaitCondition m_videoCondition;

  // 控制标志
  std::atomic<bool> m_stopRequested{false};
  std::atomic<bool> m_pauseRequested{false};
  std::atomic<bool> m_seekRequested{false};
  std::atomic<qint64> m_seekTarget{0};

  // 私有方法
  void clearQueues();
  void pushAudioPacket(AVPacket *packet);
  void pushVideoPacket(AVPacket *packet);
  
  // NEW: Private method to perform the actual blocking open operation
  bool _performOpenBlocking();
};

#endif // DEMUXER_H
