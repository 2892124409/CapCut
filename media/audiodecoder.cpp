#include "audiodecoder.h"
#include <QDebug>
#include <cstring>

AudioDecoder::AudioDecoder(QObject *parent)
    : QObject(parent)
{
    // 预分配音频缓冲区池
    m_bufferPool.resize(BUFFER_POOL_SIZE);
    for (auto& buffer : m_bufferPool) {
        buffer.data = static_cast<uint8_t*>(av_malloc(MAX_BUFFER_SIZE));
        buffer.size = MAX_BUFFER_SIZE;
        buffer.inUse = false;
    }
}

AudioDecoder::~AudioDecoder()
{
    // 释放缓冲区池
    for (auto& buffer : m_bufferPool) {
        if (buffer.data) {
            av_free(buffer.data);
            buffer.data = nullptr;
        }
    }
    m_bufferPool.clear();
    
    cleanup();
}

bool AudioDecoder::init(AVFormatContext *formatCtx, int audioStreamIndex)
{
    QMutexLocker locker(&m_mutex);

    if (!formatCtx || audioStreamIndex < 0) {
        return false;
    }

    m_streamIndex = audioStreamIndex;
    AVCodecParameters *codecPar = formatCtx->streams[audioStreamIndex]->codecpar;

    // 查找解码器
    const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        qDebug() << "无法找到音频解码器";
        return false;
    }

    // 分配解码器上下文
    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        qDebug() << "无法分配解码器上下文";
        return false;
    }
    
    m_codecCtx.reset(codecCtx, "AVCodecContext");
    avcodec_parameters_to_context(m_codecCtx.get(), codecPar);
    
    if (avcodec_open2(m_codecCtx.get(), codec, nullptr) < 0) {
        qDebug() << "无法打开音频解码器";
        m_codecCtx.reset(nullptr, "AVCodecContext");
        return false;
    }

    // 分配帧
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        qDebug() << "无法分配音频帧";
        return false;
    }
    
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

    // 创建音频输出
    m_audioSink = new QAudioSink(device, format, this);
    m_audioSink->setBufferSize(targetSampleRate * 2 * 2 * 1.0); // 1秒缓冲区
    m_audioSink->setVolume(m_volume);

    m_audioDevice = m_audioSink->start();
    if (!m_audioDevice) {
        qDebug() << "无法启动音频输出设备";
        return false;
    }

    // 初始化重采样器
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext* swrCtx = nullptr;
    swr_alloc_set_opts2(&swrCtx,
                        &out_ch_layout, AV_SAMPLE_FMT_S16, targetSampleRate,
                        &m_codecCtx->ch_layout, m_codecCtx->sample_fmt, m_codecCtx->sample_rate,
                        0, nullptr);
    if (!swrCtx) {
        qDebug() << "无法创建音频重采样器";
        return false;
    }
    
    m_swrCtx.reset(swrCtx, "SwrContext");
    swr_init(m_swrCtx.get());

    qDebug() << "音频解码器初始化成功，采样率:" << m_codecCtx->sample_rate 
             << "声道数:" << m_codecCtx->ch_layout.nb_channels;
    return true;
}

void AudioDecoder::decodePacket(AVPacket *packet)
{
    // 使用更细粒度的锁，只在必要时加锁
    if (!m_codecCtx || !m_audioDevice) {
        return;
    }

    // 检查暂停状态（需要加锁）
    {
        QMutexLocker locker(&m_mutex);
        if (m_isPaused) {
            return;
        }
    }

    if (avcodec_send_packet(m_codecCtx.get(), packet) == 0) {
        while (avcodec_receive_frame(m_codecCtx.get(), m_frame.get()) == 0) {
            // 动态获取采样率，防止变调
            int dst_rate = m_audioSink->format().sampleRate();
            if (dst_rate <= 0)
                dst_rate = 44100;

            int out_samples = av_rescale_rnd(swr_get_delay(m_swrCtx.get(), m_codecCtx->sample_rate) +
                                                 m_frame->nb_samples,
                                             dst_rate, m_codecCtx->sample_rate, AV_ROUND_UP);

            // 从缓冲区池获取可用缓冲区
            AudioBuffer* audioBuffer = nullptr;
            for (auto& buffer : m_bufferPool) {
                if (!buffer.inUse && buffer.size >= out_samples * 4) { // 4 bytes per sample (2 channels * 2 bytes)
                    buffer.inUse = true;
                    audioBuffer = &buffer;
                    break;
                }
            }

            // 如果没有可用缓冲区，动态分配一个（回退机制）
            uint8_t* buffer = nullptr;
            bool usePoolBuffer = (audioBuffer != nullptr);
            if (usePoolBuffer) {
                buffer = audioBuffer->data;
            } else {
                av_samples_alloc(&buffer, nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 0);
                qDebug() << "警告：音频缓冲区池已满，动态分配缓冲区";
            }

            int frame_count = swr_convert(m_swrCtx.get(), &buffer, out_samples,
                                          (const uint8_t **)m_frame->data, m_frame->nb_samples);

            // 写入声卡
            if (frame_count > 0 && m_audioDevice) {
                m_audioDevice->write((const char *)buffer, frame_count * 2 * 2);
            }

            // 释放缓冲区
            if (usePoolBuffer) {
                audioBuffer->inUse = false;
            } else {
                av_freep(&buffer);
            }

            emit audioDecoded();
        }
    }
}

void AudioDecoder::cleanup()
{
    QMutexLocker locker(&m_mutex);

    // 清理音频输出
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
    m_audioDevice = nullptr;

    // 智能指针会自动清理FFmpeg资源
    m_codecCtx.reset(nullptr, "AVCodecContext");
    m_frame.reset(nullptr, "AVFrame");
    m_swrCtx.reset(nullptr, "SwrContext");
    
    m_streamIndex = -1;
    m_isPaused = false;
    
    // 重置缓冲区池使用状态
    for (auto& buffer : m_bufferPool) {
        buffer.inUse = false;
    }
    
    qDebug() << "音频解码器资源清理完成";
}

void AudioDecoder::setVolume(float volume)
{
    QMutexLocker locker(&m_mutex);
    m_volume = volume;
    if (m_audioSink) {
        m_audioSink->setVolume(volume);
    }
}

void AudioDecoder::pause()
{
    QMutexLocker locker(&m_mutex);
    if (m_audioSink && !m_isPaused) {
        m_audioSink->suspend();
        m_isPaused = true;
    }
}

void AudioDecoder::resume()
{
    QMutexLocker locker(&m_mutex);
    if (m_audioSink && m_isPaused) {
        m_audioSink->resume();
        m_isPaused = false;
    }
}

void AudioDecoder::flushBuffers()
{
    QMutexLocker locker(&m_mutex);
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx.get());
        qDebug() << "音频解码器缓冲区已刷新";
    }
}
