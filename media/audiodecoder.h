#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include "ffmpeg_resource_manager.h"
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <vector>


// 前向声明
class Demuxer;

class AudioDecoder : public QThread {
  Q_OBJECT

public:
  explicit AudioDecoder(QObject *parent = nullptr);
  ~AudioDecoder() override;

  bool init(AVFormatContext *formatCtx, int audioStreamIndex);
  void cleanup();
  void setVolume(float volume);
  void requestPause();
  void requestResume();
  void requestFlush();
  void requestStop();
  void setDropUntil(qint64 ms) { m_dropUntilMs.store(ms); }

  // 设置 Demuxer 引用
  void setDemuxer(Demuxer *demuxer) { m_demuxer = demuxer; }

  // 获取音频缓冲区剩余空间
  qint64 bytesFree() const;

signals:
  void audioDecoded();
  void audioClockUpdated(qint64 positionMs);

protected:
  void run() override;

private:
  // 辅助函数
  bool recreateAudioOutput();
  void processPacket(AVPacket *packet);
  qint64 bufferedMilliseconds() const;

  FFmpeg::TrackedAVCodecContext m_codecCtx;
  FFmpeg::TrackedAVFrame m_frame;
  FFmpeg::TrackedSwrContext m_swrCtx;
  int m_streamIndex = -1;
  AVRational m_timeBase{0, 1};
  qint64 m_lastPtsMs = 0;

  QAudioSink *m_audioSink = nullptr;
  QIODevice *m_audioDevice = nullptr;
  QAudioFormat m_outputFormat;
  float m_volume = 1.0f;

  struct AudioBuffer {
    uint8_t *data = nullptr;
    int size = 0;
    bool inUse = false;
  };
  std::vector<AudioBuffer> m_bufferPool;
  static constexpr int BUFFER_POOL_SIZE = 8;
  static constexpr int MAX_BUFFER_SIZE = 192000;

  mutable QMutex m_mutex;

  // Demuxer 引用
  Demuxer *m_demuxer = nullptr;

  // 线程控制
  std::atomic<bool> m_stopRequested{false};
  std::atomic<bool> m_pauseRequested{false};
  std::atomic<bool> m_flushRequested{false};
  std::atomic<qint64> m_dropUntilMs{-1};
};

#endif // AUDIODECODER_H
