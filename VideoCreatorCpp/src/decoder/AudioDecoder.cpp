#include "AudioDecoder.h"
#include "ffmpeg_utils/AvPacketWrapper.h"
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

        qDebug() << "音频解码器信息 - 采样率: " << m_sampleRate << " 通道数: " << m_channels << " 格式: " << m_sampleFormat;

        // 使用更简单的SwrContext初始化方法
        // 输出：平面float，立体声，44100Hz（标准音频格式）
        int out_sample_rate = 44100;
        int out_channels = 2;
        AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_FLTP;

        // 设置输入参数 - 使用 av_channel_layout_default 获取默认布局，更健壮
        AVChannelLayout in_ch_layout;
        av_channel_layout_default(&in_ch_layout, m_codecContext->ch_layout.nb_channels);
        av_opt_set_chlayout(m_swrCtx, "in_chlayout", &in_ch_layout, 0);
        av_opt_set_int(m_swrCtx, "in_sample_rate", m_sampleRate, 0);
        av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_sampleFormat, 0);
        av_channel_layout_uninit(&in_ch_layout);

        // 设置输出参数
        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, out_channels);
        av_opt_set_chlayout(m_swrCtx, "out_chlayout", &out_ch_layout, 0);
        av_opt_set_int(m_swrCtx, "out_sample_rate", out_sample_rate, 0);
        av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", out_sample_fmt, 0);
        av_channel_layout_uninit(&out_ch_layout);

        qDebug() << "SwrContext 配置 - 输入: " << m_channels << "通道, " << m_sampleRate << "Hz, 格式" << m_sampleFormat;
        qDebug() << "SwrContext 配置 - 输出: " << out_channels << "通道, " << out_sample_rate << "Hz, 格式" << out_sample_fmt;

        if (swr_init(m_swrCtx) < 0)
        {
            m_errorString = "无法初始化 SwrContext";
            qDebug() << "SwrContext 初始化失败: " << m_errorString.c_str();
            swr_free(&m_swrCtx);
            cleanup();
            return false;
        }

        qDebug() << "SwrContext 初始化成功";

        return true;
    }

    bool AudioDecoder::seek(double timestamp)
    {
        if (!m_formatContext) return false;
        int64_t target_ts = static_cast<int64_t>(timestamp / av_q2d(m_formatContext->streams[m_audioStreamIndex]->time_base));
        return av_seek_frame(m_formatContext, m_audioStreamIndex, target_ts, AVSEEK_FLAG_BACKWARD) >= 0;
    }
    
    int AudioDecoder::decodeFrame(FFmpegUtils::AvFramePtr &decodedFrame) {
        if (!m_formatContext || !m_codecContext) {
            m_errorString = "解码器未打开";
            return -1;
        }
    
        auto packet = FFmpegUtils::createAvPacket();
        auto frame = FFmpegUtils::createAvFrame();
        int response;
    
        // This loop will continue as long as we can receive a valid frame
        while (true) {
            response = avcodec_receive_frame(m_codecContext, frame.get());
            if (response >= 0) {
                // Got a frame, now resample and return it
                break;
            }
    
            if (response == AVERROR_EOF) {
                m_errorString = "解码器已完全刷新";
                return 0; // EOF
            }
    
            if (response != AVERROR(EAGAIN)) {
                m_errorString = "从解码器接收帧时发生错误";
                return -1; // Actual error
            }
    
            // Need more data, so read a packet
            response = av_read_frame(m_formatContext, packet.get());
            if (response < 0) {
                // End of file or error, flush the decoder
                avcodec_send_packet(m_codecContext, nullptr);
                // Loop back to avcodec_receive_frame to get the last frames
                continue;
            }
    
            if (packet->stream_index == m_audioStreamIndex) {
                if (avcodec_send_packet(m_codecContext, packet.get()) < 0) {
                    m_errorString = "发送数据包到解码器失败";
                    av_packet_unref(packet.get());
                    return -1;
                }
            }
            av_packet_unref(packet.get());
        }
    
            // We have a frame, resample it
            AVChannelLayout out_ch_layout;
            int64_t out_sample_rate;
            AVSampleFormat out_sample_fmt;
            av_opt_get_chlayout(m_swrCtx, "out_chlayout", 0, &out_ch_layout);
            av_opt_get_int(m_swrCtx, "out_sample_rate", 0, &out_sample_rate);
            av_opt_get_sample_fmt(m_swrCtx, "out_sample_fmt", 0, &out_sample_fmt);
        
            decodedFrame = FFmpegUtils::createAvFrame();
            decodedFrame->nb_samples = static_cast<int>(av_rescale_rnd(swr_get_delay(m_swrCtx, frame->sample_rate) + frame->nb_samples, out_sample_rate, frame->sample_rate, AV_ROUND_UP));
            decodedFrame->ch_layout = out_ch_layout;
            decodedFrame->format = out_sample_fmt;
            decodedFrame->sample_rate = static_cast<int>(out_sample_rate);    
        if (av_frame_get_buffer(decodedFrame.get(), 0) < 0) {
            m_errorString = "为重采样后的音频帧分配缓冲区失败";
            av_channel_layout_uninit(&out_ch_layout);
            return -1;
        }
    
        int converted_samples = swr_convert(m_swrCtx, decodedFrame->data, decodedFrame->nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
        av_channel_layout_uninit(&out_ch_layout);
        
        if (converted_samples < 0) {
            m_errorString = "swr_convert 转换失败";
            return -1;
        }
    
        decodedFrame->nb_samples = converted_samples;
    
        if (frame->pts != AV_NOPTS_VALUE) {
            decodedFrame->pts = av_rescale_q(frame->pts, m_formatContext->streams[m_audioStreamIndex]->time_base, AVRational{1, static_cast<int>(out_sample_rate)});
        } else {
            // If PTS is not available, we can't sync. This is a problem.
            // For now, we signal an issue but don't stop. A better approach might be to estimate.
            qDebug() << "警告: 音频帧缺少PTS值";
        }
    
        return 1; // Success
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
