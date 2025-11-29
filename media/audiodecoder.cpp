#include "audiodecoder.h"
#include "demuxer.h"
#include <QDebug>
#include <cstring>

AudioDecoder::AudioDecoder(QObject *parent) : QThread(parent) {
  m_bufferPool.resize(BUFFER_POOL_SIZE);
  for (auto &buffer : m_bufferPool) {
    buffer.data = static_cast<uint8_t *>(av_malloc(MAX_BUFFER_SIZE));
    buffer.size = MAX_BUFFER_SIZE;
    buffer.inUse = false;
  }
}

AudioDecoder::~AudioDecoder() {
  requestStop();
  wait();

  for (auto &buffer : m_bufferPool) {
    if (buffer.data) {
      av_free(buffer.data);
      buffer.data = nullptr;
    }
  }
  m_bufferPool.clear();
  cleanup();
}

bool AudioDecoder::init(AVFormatContext *formatCtx, int audioStreamIndex) {
  QMutexLocker locker(&m_mutex);

  if (!formatCtx || audioStreamIndex < 0)
    return false;

  m_streamIndex = audioStreamIndex;
  AVCodecParameters *codecPar = formatCtx->streams[audioStreamIndex]->codecpar;
  m_timeBase = formatCtx->streams[audioStreamIndex]->time_base;
  m_lastPtsMs = 0;

  const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);
  if (!codec) {
    qDebug() << "AudioDecoder: 无法找到音频解码器";
    return false;
  }

  AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
  if (!codecCtx)
    return false;

  m_codecCtx.reset(codecCtx, "AVCodecContext");
  avcodec_parameters_to_context(m_codecCtx.get(), codecPar);

  if (avcodec_open2(m_codecCtx.get(), codec, nullptr) < 0) {
    m_codecCtx.reset(nullptr, "AVCodecContext");
    return false;
  }

  AVFrame *frame = av_frame_alloc();
  if (!frame)
    return false;
  m_frame.reset(frame, "AVFrame");

  // 配置音频输出格式
  int targetSampleRate = 48000;
  QAudioFormat format;
  format.setSampleRate(targetSampleRate);
  format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
  format.setSampleFormat(QAudioFormat::Int16);

  QAudioDevice device = QMediaDevices::defaultAudioOutput();
  if (!device.isFormatSupported(format)) {
    format = device.preferredFormat();
    targetSampleRate = format.sampleRate();
  }

  m_outputFormat = format;

  // 创建音频输出
  m_audioSink = new QAudioSink(device, m_outputFormat);
  m_audioSink->setBufferSize(targetSampleRate * 2 * 2 * 0.5);
  m_audioSink->setVolume(m_volume);

  m_audioDevice = m_audioSink->start();
  if (!m_audioDevice) {
    qDebug() << "AudioDecoder: 无法启动音频输出设备";
    return false;
  }

  // 初始化重采样器
  AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
  SwrContext *swrCtx = nullptr;
  swr_alloc_set_opts2(&swrCtx, &out_ch_layout, AV_SAMPLE_FMT_S16,
                      targetSampleRate, &m_codecCtx->ch_layout,
                      m_codecCtx->sample_fmt, m_codecCtx->sample_rate, 0,
                      nullptr);
  if (!swrCtx)
    return false;

  m_swrCtx.reset(swrCtx, "SwrContext");
  swr_init(m_swrCtx.get());

  qDebug() << "AudioDecoder: 初始化成功，采样率:" << targetSampleRate;
  return true;
}

void AudioDecoder::run() {
  qDebug() << "AudioDecoder: 线程启动";

  while (!m_stopRequested.load()) {
    // 处理刷新请求
    if (m_flushRequested.load()) {
      QMutexLocker locker(&m_mutex);
      if (m_codecCtx)
        avcodec_flush_buffers(m_codecCtx.get());
      if (m_audioSink)
        recreateAudioOutput();
      m_flushRequested.store(false);
      qDebug() << "AudioDecoder: 缓冲区已刷新";
      continue;
    }

    // 处理暂停
    if (m_pauseRequested.load()) {
      QThread::msleep(10);
      continue;
    }

    // 【关键修复】检查音频缓冲区是否有足够空间
    // 如果缓冲区剩余空间小于阈值，等待一段时间
    {
      QMutexLocker locker(&m_mutex);
      if (m_audioSink) {
        qint64 freeBytes = m_audioSink->bytesFree();
        // 如果剩余空间小于16KB，等待
        if (freeBytes < 16384) {
          locker.unlock();
          QThread::msleep(10);
          continue;
        }
      }
    }

    // 从 Demuxer 获取数据包
    if (!m_demuxer) {
      QThread::msleep(10);
      continue;
    }

    AVPacket *packet = m_demuxer->popAudioPacket();
    if (!packet) {
      if (m_stopRequested.load())
        break;
      continue;
    }

    // 解码数据包
    processPacket(packet);
    av_packet_free(&packet);
  }

  qDebug() << "AudioDecoder: 线程退出";
}

void AudioDecoder::processPacket(AVPacket *packet) {
  if (!m_codecCtx || !m_audioDevice)
    return;

  QMutexLocker locker(&m_mutex);

  if (avcodec_send_packet(m_codecCtx.get(), packet) == 0) {
    while (avcodec_receive_frame(m_codecCtx.get(), m_frame.get()) == 0) {
      int dst_rate = m_outputFormat.sampleRate();
      if (dst_rate <= 0)
        dst_rate = 44100;

      int out_samples = av_rescale_rnd(
          swr_get_delay(m_swrCtx.get(), m_codecCtx->sample_rate) +
              m_frame->nb_samples,
          dst_rate, m_codecCtx->sample_rate, AV_ROUND_UP);

      AudioBuffer *audioBuffer = nullptr;
      for (auto &buffer : m_bufferPool) {
        if (!buffer.inUse && buffer.size >= out_samples * 4) {
          buffer.inUse = true;
          audioBuffer = &buffer;
          break;
        }
      }

      uint8_t *buffer = nullptr;
      bool usePoolBuffer = (audioBuffer != nullptr);
      if (usePoolBuffer) {
        buffer = audioBuffer->data;
      } else {
        av_samples_alloc(&buffer, nullptr, 2, out_samples, AV_SAMPLE_FMT_S16,
                         0);
      }

      int frame_count =
          swr_convert(m_swrCtx.get(), &buffer, out_samples,
                      (const uint8_t **)m_frame->data, m_frame->nb_samples);

      qint64 ptsMs = 0;
      if (m_frame->best_effort_timestamp != AV_NOPTS_VALUE) {
        ptsMs = av_rescale_q(m_frame->best_effort_timestamp, m_timeBase,
                             AVRational{1, 1000});
      } else if (packet && packet->pts != AV_NOPTS_VALUE) {
        ptsMs = av_rescale_q(packet->pts, m_timeBase, AVRational{1, 1000});
      }
      constexpr qint64 kDropTolerance = 30;
      qint64 dropUntil = m_dropUntilMs.load();
      bool dropping = dropUntil >= 0 && ptsMs + kDropTolerance < dropUntil;

      if (frame_count > 0 && m_audioDevice && !dropping) {
        m_audioDevice->write((const char *)buffer, frame_count * 2 * 2);
      }

      if (usePoolBuffer) {
        audioBuffer->inUse = false;
      } else {
        av_freep(&buffer);
      }

      qint64 clockMs = ptsMs;
      if (dropping) {
        if (dropUntil >= 0 && ptsMs + kDropTolerance >= dropUntil) {
          m_dropUntilMs.store(-1);
        }
      } else {
        qint64 delayMs = bufferedMilliseconds();
        if (delayMs > 0)
          clockMs = qMax<qint64>(0, ptsMs - delayMs);
        emit audioClockUpdated(clockMs);
        emit audioDecoded();
      }
      m_lastPtsMs = ptsMs;
    }
  }
}

qint64 AudioDecoder::bufferedMilliseconds() const {
  if (!m_audioSink)
    return 0;

  const int sampleRate = m_outputFormat.sampleRate();
  const int bytesPerFrame = m_outputFormat.bytesPerFrame();
  if (sampleRate <= 0 || bytesPerFrame <= 0)
    return 0;

  const qint64 bytesPerSec = qint64(sampleRate) * bytesPerFrame;
  qint64 queued = m_audioSink->bufferSize() - m_audioSink->bytesFree();
  if (queued < 0)
    queued = 0;

  return bytesPerSec > 0 ? queued * 1000 / bytesPerSec : 0;
}

bool AudioDecoder::recreateAudioOutput() {
  if (m_audioSink) {
    m_audioSink->stop();
    delete m_audioSink;
    m_audioSink = nullptr;
  }
  m_audioDevice = nullptr;

  QAudioDevice device = QMediaDevices::defaultAudioOutput();
  m_audioSink = new QAudioSink(device, m_outputFormat);

  int bytesPerSec = m_outputFormat.sampleRate() * 2 * 2;
  m_audioSink->setBufferSize(bytesPerSec * 0.5);
  m_audioSink->setVolume(m_volume);

  m_audioDevice = m_audioSink->start();
  if (!m_audioDevice) {
    qDebug() << "AudioDecoder: 重建音频设备失败";
    return false;
  }
  return true;
}

qint64 AudioDecoder::bytesFree() const {
  QMutexLocker locker(&m_mutex);
  if (m_audioSink) {
    return m_audioSink->bytesFree();
  }
  return 0;
}

void AudioDecoder::cleanup() {
  QMutexLocker locker(&m_mutex);

  if (m_audioSink) {
    m_audioSink->stop();
    delete m_audioSink;
    m_audioSink = nullptr;
  }
  m_audioDevice = nullptr;

  m_codecCtx.reset(nullptr, "AVCodecContext");
  m_frame.reset(nullptr, "AVFrame");
  m_swrCtx.reset(nullptr, "SwrContext");

  m_streamIndex = -1;

  for (auto &buffer : m_bufferPool) {
    buffer.inUse = false;
  }
}

void AudioDecoder::setVolume(float volume) {
  QMutexLocker locker(&m_mutex);
  m_volume = volume;
  if (m_audioSink)
    m_audioSink->setVolume(volume);
}

void AudioDecoder::requestPause() {
  m_pauseRequested.store(true);
  QMutexLocker locker(&m_mutex);
  if (m_audioSink)
    m_audioSink->suspend();
}

void AudioDecoder::requestResume() {
  m_pauseRequested.store(false);
  QMutexLocker locker(&m_mutex);
  if (m_audioSink)
    m_audioSink->resume();
}

void AudioDecoder::requestFlush() { m_flushRequested.store(true); }

void AudioDecoder::requestStop() { m_stopRequested.store(true); }
