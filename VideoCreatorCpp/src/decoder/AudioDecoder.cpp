#include "AudioDecoder.h"
#include <iostream>

namespace VideoCreator
{

    AudioDecoder::AudioDecoder()
        : m_formatContext(nullptr), m_codecContext(nullptr), m_audioStreamIndex(-1),
          m_sampleRate(0), m_channels(0), m_sampleFormat(AV_SAMPLE_FMT_NONE), m_duration(0)
    {
    }

    AudioDecoder::~AudioDecoder()
    {
        cleanup();
    }

    bool AudioDecoder::open(const std::string &filePath)
    {
        // 打开输入文件
        if (avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr) < 0)
        {
            m_errorString = "无法打开音频文件: " + filePath;
            return false;
        }

        // 查找流信息
        if (avformat_find_stream_info(m_formatContext, nullptr) < 0)
        {
            m_errorString = "无法获取流信息";
            cleanup();
            return false;
        }

        // 查找音频流
        m_audioStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (m_audioStreamIndex < 0)
        {
            m_errorString = "未找到音频流";
            cleanup();
            return false;
        }

        // 获取音频流
        AVStream *audioStream = m_formatContext->streams[m_audioStreamIndex];

        // 查找解码器
        const AVCodec *codec = avcodec_find_decoder(audioStream->codecpar->codec_id);
        if (!codec)
        {
            m_errorString = "未找到解码器";
            cleanup();
            return false;
        }

        // 创建解码器上下文
        m_codecContext = avcodec_alloc_context3(codec);
        if (!m_codecContext)
        {
            m_errorString = "无法创建解码器上下文";
            cleanup();
            return false;
        }

        // 复制参数到解码器上下文
        if (avcodec_parameters_to_context(m_codecContext, audioStream->codecpar) < 0)
        {
            m_errorString = "无法复制解码器参数";
            cleanup();
            return false;
        }

        // 打开解码器
        if (avcodec_open2(m_codecContext, codec, nullptr) < 0)
        {
            m_errorString = "无法打开解码器";
            cleanup();
            return false;
        }

        // 保存音频信息
        m_sampleRate = m_codecContext->sample_rate;
        m_channels = m_codecContext->ch_layout.nb_channels;
        m_sampleFormat = m_codecContext->sample_fmt;
        m_duration = audioStream->duration;

        return true;
    }

    std::vector<uint8_t> AudioDecoder::decode()
    {
        if (!m_formatContext || !m_codecContext)
        {
            m_errorString = "解码器未打开";
            return {};
        }

        std::vector<uint8_t> audioData;
        AVPacket *packet = av_packet_alloc();
        if (!packet)
        {
            m_errorString = "无法分配数据包";
            return {};
        }

        FFmpegUtils::AvFramePtr frame = FFmpegUtils::createAvFrame();
        if (!frame)
        {
            m_errorString = "无法创建帧";
            av_packet_free(&packet);
            return {};
        }

        int response;
        while (av_read_frame(m_formatContext, packet) >= 0)
        {
            if (packet->stream_index == m_audioStreamIndex)
            {
                response = avcodec_send_packet(m_codecContext, packet);
                if (response < 0)
                {
                    m_errorString = "发送数据包到解码器失败";
                    av_packet_free(&packet);
                    return {};
                }

                while (response >= 0)
                {
                    response = avcodec_receive_frame(m_codecContext, frame.get());
                    if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
                    {
                        break;
                    }
                    else if (response < 0)
                    {
                        m_errorString = "从解码器接收帧失败";
                        av_packet_free(&packet);
                        return {};
                    }

                    // 将音频数据复制到vector
                    int dataSize = av_samples_get_buffer_size(nullptr, m_channels,
                                                              frame->nb_samples,
                                                              m_sampleFormat, 1);
                    if (dataSize > 0)
                    {
                        size_t currentSize = audioData.size();
                        audioData.resize(currentSize + dataSize);
                        memcpy(audioData.data() + currentSize, frame->data[0], dataSize);
                    }
                }
            }
            av_packet_unref(packet);
        }

        // 刷新解码器
        avcodec_send_packet(m_codecContext, nullptr);
        while (response >= 0)
        {
            response = avcodec_receive_frame(m_codecContext, frame.get());
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
            {
                break;
            }
            else if (response < 0)
            {
                break;
            }

            // 将音频数据复制到vector
            int dataSize = av_samples_get_buffer_size(nullptr, m_channels,
                                                      frame->nb_samples,
                                                      m_sampleFormat, 1);
            if (dataSize > 0)
            {
                size_t currentSize = audioData.size();
                audioData.resize(currentSize + dataSize);
                memcpy(audioData.data() + currentSize, frame->data[0], dataSize);
            }
        }

        av_packet_free(&packet);
        return audioData;
    }

    void AudioDecoder::close()
    {
        cleanup();
    }

    void AudioDecoder::cleanup()
    {
        if (m_codecContext)
        {
            avcodec_free_context(&m_codecContext);
            m_codecContext = nullptr;
        }

        if (m_formatContext)
        {
            avformat_close_input(&m_formatContext);
            m_formatContext = nullptr;
        }

        m_audioStreamIndex = -1;
        m_sampleRate = 0;
        m_channels = 0;
        m_sampleFormat = AV_SAMPLE_FMT_NONE;
        m_duration = 0;
    }

} // namespace VideoCreator