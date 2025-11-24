#include "AudioDecoder.h"
#include <QDebug>

namespace VideoCreator
{

    AudioDecoder::AudioDecoder()
        : m_formatContext(nullptr), m_codecContext(nullptr), m_audioStreamIndex(-1),
          m_sampleRate(0), m_channels(0), m_sampleFormat(AV_SAMPLE_FMT_NONE), m_duration(0), m_swrCtx(nullptr)
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
        // 使用 codecContext 的 ch_layout.nb_channels（与当前 FFmpeg 头兼容）
        m_channels = m_codecContext->ch_layout.nb_channels > 0 ? m_codecContext->ch_layout.nb_channels : 2;
        m_sampleFormat = m_codecContext->sample_fmt;
        m_duration = audioStream->duration;

        // 初始化 SwrContext：使用 swr_alloc + av_opt_set_int 来兼容不同 FFmpeg 版本
        m_swrCtx = swr_alloc();
        if (!m_swrCtx)
        {
            m_errorString = "无法分配 SwrContext";
            cleanup();
            return false;
        }

        // 输入参数来自 codecContext 的通道数，映射到常用通道布局宏
        int64_t in_ch_layout = 0;
        if (m_channels == 1)
            in_ch_layout = AV_CH_LAYOUT_MONO;
        else if (m_channels == 2)
            in_ch_layout = AV_CH_LAYOUT_STEREO;
        else
            in_ch_layout = AV_CH_LAYOUT_STEREO; // 默认使用立体声

        av_opt_set_int(m_swrCtx, "in_channel_layout", in_ch_layout, 0);
        av_opt_set_int(m_swrCtx, "in_sample_rate", m_sampleRate, 0);
        av_opt_set_int(m_swrCtx, "in_sample_fmt", m_sampleFormat, 0);

        // 输出：交错 float，使用输入通道数的常见布局
        int64_t out_ch_layout = 0;
        if (m_channels == 1)
            out_ch_layout = AV_CH_LAYOUT_MONO;
        else if (m_channels == 2)
            out_ch_layout = AV_CH_LAYOUT_STEREO;
        else
            out_ch_layout = AV_CH_LAYOUT_STEREO; // 默认使用立体声
        av_opt_set_int(m_swrCtx, "out_channel_layout", out_ch_layout, 0);
        av_opt_set_int(m_swrCtx, "out_sample_rate", m_sampleRate, 0);
        av_opt_set_int(m_swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

        if (swr_init(m_swrCtx) < 0)
        {
            m_errorString = "无法初始化 SwrContext";
            swr_free(&m_swrCtx);
            cleanup();
            return false;
        }

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

        int response = 0;
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

                for (;;)
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

                    // 使用 swr_convert 将帧转换为交错 float
                    if (!m_swrCtx)
                    {
                        m_errorString = "SwrContext 未初始化";
                        av_packet_free(&packet);
                        return {};
                    }

                    int64_t delay = swr_get_delay(m_swrCtx, m_sampleRate);
                    int max_out_samples = av_rescale_rnd(delay + frame->nb_samples, m_sampleRate, m_sampleRate, AV_ROUND_UP);

                    uint8_t **out_data = nullptr;
                    int out_linesize = 0;
                    if (av_samples_alloc_array_and_samples(&out_data, &out_linesize, m_channels, max_out_samples, AV_SAMPLE_FMT_FLT, 0) < 0)
                    {
                        m_errorString = "分配输出样本缓冲区失败";
                        av_packet_free(&packet);
                        return {};
                    }

                    int converted_samples = swr_convert(m_swrCtx, out_data, max_out_samples,
                                                        (const uint8_t **)frame->data, frame->nb_samples);

                    if (converted_samples < 0)
                    {
                        m_errorString = "swr_convert 转换失败";
                        av_freep(&out_data[0]);
                        av_freep(&out_data);
                        av_packet_free(&packet);
                        return {};
                    }

                    int out_buffer_size = av_samples_get_buffer_size(&out_linesize, m_channels, converted_samples, AV_SAMPLE_FMT_FLT, 1);
                    if (out_buffer_size > 0)
                    {
                        size_t currentSize = audioData.size();
                        audioData.resize(currentSize + out_buffer_size);
                        memcpy(audioData.data() + currentSize, out_data[0], out_buffer_size);
                    }

                    av_freep(&out_data[0]);
                    av_freep(&out_data);
                }
            }
            av_packet_unref(packet);
        }

        // 刷新解码器
        avcodec_send_packet(m_codecContext, nullptr);
        for (;;)
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

            if (!m_swrCtx)
            {
                break;
            }

            int64_t delay = swr_get_delay(m_swrCtx, m_sampleRate);
            int max_out_samples = av_rescale_rnd(delay + frame->nb_samples, m_sampleRate, m_sampleRate, AV_ROUND_UP);

            uint8_t **out_data = nullptr;
            int out_linesize = 0;
            if (av_samples_alloc_array_and_samples(&out_data, &out_linesize, m_channels, max_out_samples, AV_SAMPLE_FMT_FLT, 0) < 0)
            {
                break;
            }

            int converted_samples = swr_convert(m_swrCtx, out_data, max_out_samples,
                                                (const uint8_t **)frame->data, frame->nb_samples);
            if (converted_samples > 0)
            {
                int out_buffer_size = av_samples_get_buffer_size(&out_linesize, m_channels, converted_samples, AV_SAMPLE_FMT_FLT, 1);
                if (out_buffer_size > 0)
                {
                    size_t currentSize = audioData.size();
                    audioData.resize(currentSize + out_buffer_size);
                    memcpy(audioData.data() + currentSize, out_data[0], out_buffer_size);
                }
            }

            av_freep(&out_data[0]);
            av_freep(&out_data);
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

        if (m_swrCtx)
        {
            swr_free(&m_swrCtx);
            m_swrCtx = nullptr;
        }

        m_audioStreamIndex = -1;
        m_sampleRate = 0;
        m_channels = 0;
        m_sampleFormat = AV_SAMPLE_FMT_NONE;
        m_duration = 0;
    }

} // namespace VideoCreator
