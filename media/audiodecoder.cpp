#include "audiodecoder.h"
#include <QDebug>
#include <cstring>

AudioDecoder::AudioDecoder(QObject *parent)
    : QObject(parent)
{
    m_bufferPool.resize(BUFFER_POOL_SIZE);
    for (auto &buffer : m_bufferPool)
    {
        buffer.data = static_cast<uint8_t *>(av_malloc(MAX_BUFFER_SIZE));
        buffer.size = MAX_BUFFER_SIZE;
        buffer.inUse = false;
    }
}

AudioDecoder::~AudioDecoder()
{
    for (auto &buffer : m_bufferPool)
    {
        if (buffer.data)
        {
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

    if (!formatCtx || audioStreamIndex < 0)
        return false;

    m_streamIndex = audioStreamIndex;
    AVCodecParameters *codecPar = formatCtx->streams[audioStreamIndex]->codecpar;

    const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec)
    {
        qDebug() << "无法找到音频解码器";
        return false;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx)
        return false;

    m_codecCtx.reset(codecCtx, "AVCodecContext");
    avcodec_parameters_to_context(m_codecCtx.get(), codecPar);

    if (avcodec_open2(m_codecCtx.get(), codec, nullptr) < 0)
    {
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
    if (!device.isFormatSupported(format))
    {
        format = device.preferredFormat();
        targetSampleRate = format.sampleRate();
    }

    // 【关键】保存最终协商的格式，供后续重建使用
    m_outputFormat = format;

    // 创建音频输出
    m_audioSink = new QAudioSink(device, m_outputFormat, this);
    m_audioSink->setBufferSize(targetSampleRate * 2 * 2 * 0.5); // 0.5秒缓冲区
    m_audioSink->setVolume(m_volume);

    m_audioDevice = m_audioSink->start();
    if (!m_audioDevice)
    {
        qDebug() << "无法启动音频输出设备";
        return false;
    }

    // 初始化重采样器
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext *swrCtx = nullptr;
    swr_alloc_set_opts2(&swrCtx,
                        &out_ch_layout, AV_SAMPLE_FMT_S16, targetSampleRate,
                        &m_codecCtx->ch_layout, m_codecCtx->sample_fmt, m_codecCtx->sample_rate,
                        0, nullptr);
    if (!swrCtx)
        return false;

    m_swrCtx.reset(swrCtx, "SwrContext");
    swr_init(m_swrCtx.get());

    qDebug() << "音频解码器初始化成功，采样率:" << targetSampleRate;
    return true;
}

// 【新增】重建音频输出函数，专门用于 Flush 之后恢复
bool AudioDecoder::recreateAudioOutput()
{
    if (m_audioSink)
    {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
    m_audioDevice = nullptr;

    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    m_audioSink = new QAudioSink(device, m_outputFormat, this);

    // 保持 0.5 秒缓冲区
    int bytesPerSec = m_outputFormat.sampleRate() * 2 * 2;
    m_audioSink->setBufferSize(bytesPerSec * 0.5);
    m_audioSink->setVolume(m_volume);

    m_audioDevice = m_audioSink->start();
    if (!m_audioDevice)
    {
        qDebug() << "重建音频设备失败";
        return false;
    }
    return true;
}

void AudioDecoder::decodePacket(AVPacket *packet)
{
    if (!m_codecCtx || !m_audioDevice)
        return;

    // 细粒度锁，防止与 flushBuffers 冲突
    QMutexLocker locker(&m_mutex);
    if (m_isPaused)
        return;

    if (avcodec_send_packet(m_codecCtx.get(), packet) == 0)
    {
        while (avcodec_receive_frame(m_codecCtx.get(), m_frame.get()) == 0)
        {
            int dst_rate = m_outputFormat.sampleRate(); // 使用保存的采样率
            if (dst_rate <= 0)
                dst_rate = 44100;

            int out_samples = av_rescale_rnd(swr_get_delay(m_swrCtx.get(), m_codecCtx->sample_rate) +
                                                 m_frame->nb_samples,
                                             dst_rate, m_codecCtx->sample_rate, AV_ROUND_UP);

            AudioBuffer *audioBuffer = nullptr;
            for (auto &buffer : m_bufferPool)
            {
                if (!buffer.inUse && buffer.size >= out_samples * 4)
                {
                    buffer.inUse = true;
                    audioBuffer = &buffer;
                    break;
                }
            }

            uint8_t *buffer = nullptr;
            bool usePoolBuffer = (audioBuffer != nullptr);
            if (usePoolBuffer)
            {
                buffer = audioBuffer->data;
            }
            else
            {
                av_samples_alloc(&buffer, nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 0);
            }

            int frame_count = swr_convert(m_swrCtx.get(), &buffer, out_samples,
                                          (const uint8_t **)m_frame->data, m_frame->nb_samples);

            if (frame_count > 0 && m_audioDevice)
            {
                // 这里如果 device 正在重建中，m_audioDevice 可能变化，但有 mutex 保护
                m_audioDevice->write((const char *)buffer, frame_count * 2 * 2);
            }

            if (usePoolBuffer)
            {
                audioBuffer->inUse = false;
            }
            else
            {
                av_freep(&buffer);
            }

            emit audioDecoded();
        }
    }
}

qint64 AudioDecoder::bytesFree() const
{
    QMutexLocker locker(&m_mutex);
    if (m_audioSink)
    {
        return m_audioSink->bytesFree();
    }
    return 0;
}

void AudioDecoder::cleanup()
{
    QMutexLocker locker(&m_mutex);

    if (m_audioSink)
    {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
    }
    m_audioDevice = nullptr;

    m_codecCtx.reset(nullptr, "AVCodecContext");
    m_frame.reset(nullptr, "AVFrame");
    m_swrCtx.reset(nullptr, "SwrContext");

    m_streamIndex = -1;
    m_isPaused = false;

    for (auto &buffer : m_bufferPool)
    {
        buffer.inUse = false;
    }
}

void AudioDecoder::setVolume(float volume)
{
    QMutexLocker locker(&m_mutex);
    m_volume = volume;
    if (m_audioSink)
        m_audioSink->setVolume(volume);
}

void AudioDecoder::pause()
{
    QMutexLocker locker(&m_mutex);
    if (m_audioSink && !m_isPaused)
    {
        m_audioSink->suspend();
        m_isPaused = true;
    }
}

void AudioDecoder::resume()
{
    QMutexLocker locker(&m_mutex);
    if (m_audioSink && m_isPaused)
    {
        m_audioSink->resume();
        m_isPaused = false;
    }
}

void AudioDecoder::flushBuffers()
{
    QMutexLocker locker(&m_mutex);

    // 1. 刷新 FFmpeg 缓冲区
    if (m_codecCtx)
        avcodec_flush_buffers(m_codecCtx.get());

    // 2. 【核心修复】直接销毁并重建 QAudioSink
    // 这是解决 Windows 下 reset() 崩溃、stop() 死锁最稳妥的方法
    // 虽然有微小的性能开销，但对于拖动操作来说可以忽略不计
    if (m_audioSink)
    {
        recreateAudioOutput();
    }

    qDebug() << "音频缓冲区已刷新 (Recreated)";
}