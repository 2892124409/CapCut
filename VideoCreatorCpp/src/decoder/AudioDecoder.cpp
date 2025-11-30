#include "AudioDecoder.h"
#include "ffmpeg_utils/AvPacketWrapper.h"
#include <QDebug>
#include <sstream>

namespace VideoCreator
{

    AudioDecoder::AudioDecoder()
        : m_formatContext(nullptr), m_codecContext(nullptr), m_audioStreamIndex(-1),
          m_swrCtx(nullptr), m_filterGraph(nullptr), m_bufferSrcCtx(nullptr), m_bufferSinkCtx(nullptr),
          m_effectsEnabled(false), m_sampleRate(0), m_channels(0), m_sampleFormat(AV_SAMPLE_FMT_NONE), m_duration(0)
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
        m_channels = m_codecContext->ch_layout.nb_channels > 0 ? m_codecContext->ch_layout.nb_channels : 2;
        m_sampleFormat = m_codecContext->sample_fmt;
        m_duration = audioStream->duration;

        // 初始化 SwrContext
        m_swrCtx = swr_alloc();
        if (!m_swrCtx)
        {
            m_errorString = "无法分配 SwrContext";
            cleanup();
            return false;
        }
        
        AVChannelLayout in_ch_layout, out_ch_layout;
        av_channel_layout_default(&in_ch_layout, m_codecContext->ch_layout.nb_channels);
        av_channel_layout_default(&out_ch_layout, 2);

        av_opt_set_chlayout(m_swrCtx, "in_chlayout", &in_ch_layout, 0);
        av_opt_set_int(m_swrCtx, "in_sample_rate", m_sampleRate, 0);
        av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_sampleFormat, 0);
        
        av_opt_set_chlayout(m_swrCtx, "out_chlayout", &out_ch_layout, 0);
        av_opt_set_int(m_swrCtx, "out_sample_rate", 44100, 0);
        av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
        
        av_channel_layout_uninit(&in_ch_layout);
        av_channel_layout_uninit(&out_ch_layout);

        if (swr_init(m_swrCtx) < 0)
        {
            m_errorString = "无法初始化 SwrContext";
            swr_free(&m_swrCtx);
            cleanup();
            return false;
        }

        return true;
    }

    bool AudioDecoder::applyVolumeEffect(const SceneConfig& sceneConfig)
    {
        m_effectsEnabled = sceneConfig.effects.volume_mix.enabled || sceneConfig.resources.audio.volume != 1.0;
        if (m_effectsEnabled) {
            return initFilterGraph(sceneConfig);
        }
        return true;
    }

    bool AudioDecoder::initFilterGraph(const SceneConfig& sceneConfig)
    {
        // 仅清理旧的滤镜图，避免破坏已打开的解码/重采样上下文
        if (m_filterGraph) {
            avfilter_graph_free(&m_filterGraph);
            m_filterGraph = nullptr;
            m_bufferSrcCtx = nullptr;
            m_bufferSinkCtx = nullptr;
        }

        m_filterGraph = avfilter_graph_alloc();
        if (!m_filterGraph) {
            m_errorString = "无法分配 Filter Graph";
            return false;
        }

        // 1. 定义源和汇
        const AVFilter *abuffer_src = avfilter_get_by_name("abuffer");
        const AVFilter *abuffer_sink = avfilter_get_by_name("abuffersink");

        // 2. 创建源 filter 实例
        AVChannelLayout out_ch_layout;
        int64_t out_sample_rate;
        av_opt_get_chlayout(m_swrCtx, "out_chlayout", 0, &out_ch_layout);
        av_opt_get_int(m_swrCtx, "out_sample_rate", 0, &out_sample_rate);

        std::stringstream args;
        args << "time_base=1/" << out_sample_rate
             << ":sample_rate=" << out_sample_rate
             << ":sample_fmt=" << av_get_sample_fmt_name(AV_SAMPLE_FMT_FLTP)
             << ":channel_layout=" << out_ch_layout.u.mask;
        
        int ret = avfilter_graph_create_filter(&m_bufferSrcCtx, abuffer_src, "in", args.str().c_str(), nullptr, m_filterGraph);
        if (ret < 0) {
            m_errorString = "无法创建源 filter";
            av_channel_layout_uninit(&out_ch_layout);
            return false;
        }
        av_channel_layout_uninit(&out_ch_layout);

        // 3. 创建汇 filter 实例
        ret = avfilter_graph_create_filter(&m_bufferSinkCtx, abuffer_sink, "out", nullptr, nullptr, m_filterGraph);
        if (ret < 0) {
            m_errorString = "无法创建汇 filter";
            return false;
        }
        
        // 4. 构建 filter chain 描述
        std::stringstream filter_spec;
        const auto& vol_mix = sceneConfig.effects.volume_mix;
        if (vol_mix.enabled) {
            if (vol_mix.fade_in > 0) {
                filter_spec << "afade=t=in:d=" << vol_mix.fade_in << ",";
            }
            if (vol_mix.fade_out > 0) {
                double scene_duration = getDuration();
                if (scene_duration > 0 && scene_duration > vol_mix.fade_out) {
                    filter_spec << "afade=t=out:st=" << (scene_duration - vol_mix.fade_out) << ":d=" << vol_mix.fade_out << ",";
                }
            }
        }
        filter_spec << "volume=" << sceneConfig.resources.audio.volume;

        // 5. 解析并链接 filter chain
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs = avfilter_inout_alloc();
        AVFilterContext *last_filter = m_bufferSrcCtx;

        if (!outputs || !inputs) {
            m_errorString = "无法分配 filter inout";
             avfilter_inout_free(&outputs);
             avfilter_inout_free(&inputs);
            return false;
        }

        outputs->name = av_strdup("in");
        outputs->filter_ctx = m_bufferSrcCtx;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = m_bufferSinkCtx;
        inputs->pad_idx = 0;
        inputs->next = nullptr;
        
        ret = avfilter_graph_parse_ptr(m_filterGraph, filter_spec.str().c_str(), &inputs, &outputs, nullptr);
        if (ret < 0) {
            m_errorString = "无法解析 filter chain";
            avfilter_inout_free(&outputs);
            avfilter_inout_free(&inputs);
            return false;
        }

        // 6. 验证并提交配置
        ret = avfilter_graph_config(m_filterGraph, nullptr);
        if (ret < 0) {
            m_errorString = "无法配置 filter graph";
            return false;
        }

        return true;
    }

    bool AudioDecoder::seek(double timestamp)
    {
        if (!m_formatContext) return false;
        int64_t target_ts = static_cast<int64_t>(timestamp / av_q2d(m_formatContext->streams[m_audioStreamIndex]->time_base));
        return av_seek_frame(m_formatContext, m_audioStreamIndex, target_ts, AVSEEK_FLAG_BACKWARD) >= 0;
    }
    
    int AudioDecoder::decodeFrame(FFmpegUtils::AvFramePtr &outFrame) {
        if (!m_formatContext || !m_codecContext) {
            m_errorString = "解码器未打开";
            return -1;
        }

        if (m_effectsEnabled) {
            // 如果启用了效果，从filter graph获取帧
            auto filtered_frame = FFmpegUtils::createAvFrame();
            int ret = av_buffersink_get_frame(m_bufferSinkCtx, filtered_frame.get());
            if (ret == 0) {
                outFrame = std::move(filtered_frame);
                return 1;
            }
            if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                m_errorString = "从filter graph获取帧失败";
                return -1;
            }
            // 如果是 EAGAIN 或 EOF, 我们需要向下游发送更多数据
        }
    
        auto packet = FFmpegUtils::createAvPacket();
        auto raw_frame = FFmpegUtils::createAvFrame();
        int response;
    
        while (true) {
            response = avcodec_receive_frame(m_codecContext, raw_frame.get());
            if (response >= 0) {
                break; // 成功获取一个原始帧
            }
    
            if (response == AVERROR_EOF) {
                if (m_effectsEnabled) {
                    int add_frame_ret = av_buffersrc_add_frame(m_bufferSrcCtx, nullptr); // 发送EOF到filter graph
                    if (add_frame_ret < 0) {
                        m_errorString = "发送EOF到filter graph失败";
                        return -1; // Indicate error
                    }
                    auto filtered_frame = FFmpegUtils::createAvFrame();
                    int ret = av_buffersink_get_frame(m_bufferSinkCtx, filtered_frame.get());
                    if (ret == 0) {
                        outFrame = std::move(filtered_frame);
                        return 1;
                    }
                }
                m_errorString = "解码器已完全刷新";
                return 0; // 文件结束
            }
    
            if (response != AVERROR(EAGAIN)) {
                m_errorString = "从解码器接收帧时发生错误";
                return -1;
            }
    
            // 需要更多数据包
            response = av_read_frame(m_formatContext, packet.get());
            if (response < 0) {
                avcodec_send_packet(m_codecContext, nullptr); // Flush解码器
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
    
        // 重采样
        auto resampled_frame = FFmpegUtils::createAvFrame();
        AVChannelLayout out_ch_layout;
        int64_t out_sample_rate;
        AVSampleFormat out_sample_fmt;
        av_opt_get_chlayout(m_swrCtx, "out_chlayout", 0, &out_ch_layout);
        av_opt_get_int(m_swrCtx, "out_sample_rate", 0, &out_sample_rate);
        av_opt_get_sample_fmt(m_swrCtx, "out_sample_fmt", 0, &out_sample_fmt);

        resampled_frame->nb_samples = static_cast<int>(av_rescale_rnd(swr_get_delay(m_swrCtx, raw_frame->sample_rate) + raw_frame->nb_samples, out_sample_rate, raw_frame->sample_rate, AV_ROUND_UP));
        resampled_frame->ch_layout = out_ch_layout;
        resampled_frame->format = out_sample_fmt;
        resampled_frame->sample_rate = static_cast<int>(out_sample_rate);    
        
        if (av_frame_get_buffer(resampled_frame.get(), 0) < 0) {
            m_errorString = "为重采样后的音频帧分配缓冲区失败";
            av_channel_layout_uninit(&out_ch_layout);
            return -1;
        }
    
        int converted_samples = swr_convert(m_swrCtx, resampled_frame->data, resampled_frame->nb_samples, (const uint8_t **)raw_frame->data, raw_frame->nb_samples);
        av_channel_layout_uninit(&out_ch_layout);
        
        if (converted_samples < 0) {
            m_errorString = "swr_convert 转换失败";
            return -1;
        }
        resampled_frame->nb_samples = converted_samples;

        if (raw_frame->pts != AV_NOPTS_VALUE) {
            resampled_frame->pts = av_rescale_q(raw_frame->pts, m_formatContext->streams[m_audioStreamIndex]->time_base, AVRational{1, static_cast<int>(out_sample_rate)});
        }
    
        if (m_effectsEnabled) {
            // 将重采样后的帧发送到 filter graph
            if (av_buffersrc_add_frame(m_bufferSrcCtx, resampled_frame.get()) < 0) {
                m_errorString = "发送帧到 filter graph 失败";
                return -1;
            }
            // 再次尝试从 sink 获取处理过的帧
            return decodeFrame(outFrame);
        } else {
            // 未启用效果，直接返回重采样后的帧
            outFrame = std::move(resampled_frame);
            return 1;
        }
    }
    
    double AudioDecoder::getDuration() const
    {
        if (!m_formatContext || m_audioStreamIndex < 0) {
            return 0.0;
        }
        int64_t duration_ts = m_formatContext->streams[m_audioStreamIndex]->duration;
        if (duration_ts == AV_NOPTS_VALUE) {
            duration_ts = m_formatContext->duration;
        }
        if (duration_ts != AV_NOPTS_VALUE) {
            return (double)duration_ts * av_q2d(m_formatContext->streams[m_audioStreamIndex]->time_base);
        }
        return 0.0;
    }
    
    void AudioDecoder::close()
    {
        cleanup();
    }

    void AudioDecoder::cleanup()
    {
        if (m_codecContext) {
            avcodec_free_context(&m_codecContext);
            m_codecContext = nullptr;
        }
        if (m_formatContext) {
            avformat_close_input(&m_formatContext);
            m_formatContext = nullptr;
        }
        if (m_swrCtx) {
            swr_free(&m_swrCtx);
            m_swrCtx = nullptr;
        }
        if (m_filterGraph) {
            avfilter_graph_free(&m_filterGraph);
            m_filterGraph = nullptr; // m_bufferSrcCtx and m_bufferSinkCtx are freed with the graph
        }
        m_audioStreamIndex = -1;
    }

} // namespace VideoCreator
