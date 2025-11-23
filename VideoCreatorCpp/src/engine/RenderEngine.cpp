#include "RenderEngine.h"
#include "decoder/ImageDecoder.h"
#include "decoder/AudioDecoder.h"
#include "filter/EffectProcessor.h"
#include "ffmpeg_utils/AvFrameWrapper.h"
#include "ffmpeg_utils/AvPacketWrapper.h"
#include <iostream>
#include <cmath>

namespace VideoCreator
{

    RenderEngine::RenderEngine()
        : m_outputContext(nullptr), m_videoCodecContext(nullptr), m_audioCodecContext(nullptr),
          m_videoStream(nullptr), m_audioStream(nullptr), m_frameCount(0), m_progress(0)
    {
    }

    RenderEngine::~RenderEngine()
    {
        cleanup();
    }

    bool RenderEngine::initialize(const ProjectConfig &config)
    {
        m_config = config;
        m_frameCount = 0;
        m_progress = 0;

        // 创建输出上下文
        if (!createOutputContext())
        {
            return false;
        }

        // 创建视频流
        if (!createVideoStream())
        {
            return false;
        }

        // 创建音频流 (可选)
        if (!createAudioStream())
        {
            std::cout << "音频流创建失败，继续视频渲染" << std::endl;
        }

        // 写入文件头
        if (avformat_write_header(m_outputContext, nullptr) < 0)
        {
            m_errorString = "写入文件头失败";
            return false;
        }

        return true;
    }

    bool RenderEngine::render()
    {
        double currentTime = 0.0;

        // 渲染所有场景
        for (const auto &scene : m_config.scenes)
        {
            if (!renderScene(scene, currentTime))
            {
                return false;
            }
            currentTime += scene.duration;
        }

        // 在写入文件尾之前，flush 编码器以输出剩余包
        if (m_videoCodecContext && m_videoStream)
        {
            if (!flushEncoder(m_videoCodecContext, m_videoStream))
            {
                return false;
            }
        }

        if (m_audioCodecContext && m_audioStream)
        {
            if (!flushEncoder(m_audioCodecContext, m_audioStream))
            {
                return false;
            }
        }

        // 写入文件尾
        if (av_write_trailer(m_outputContext) < 0)
        {
            m_errorString = "写入文件尾失败";
            return false;
        }

        std::cout << "视频渲染完成！总帧数: " << m_frameCount << std::endl;
        return true;
    }

    bool RenderEngine::createOutputContext()
    {
        // 创建输出格式上下文
        if (avformat_alloc_output_context2(&m_outputContext, nullptr, nullptr,
                                           m_config.project.output_path.c_str()) < 0)
        {
            m_errorString = "创建输出上下文失败";
            return false;
        }

        if (!m_outputContext)
        {
            m_errorString = "输出上下文为空";
            return false;
        }

        // 如果需要打开输出 IO (非 AVFMT_NOFILE)，则打开输出文件
        if (!(m_outputContext->oformat->flags & AVFMT_NOFILE))
        {
            if (avio_open(&m_outputContext->pb, m_config.project.output_path.c_str(), AVIO_FLAG_WRITE) < 0)
            {
                m_errorString = "无法打开输出文件";
                return false;
            }
        }

        return true;
    }

    bool RenderEngine::createVideoStream()
    {
        // 查找视频编码器
        const AVCodec *videoCodec = avcodec_find_encoder_by_name(m_config.global_effects.video_encoding.codec.c_str());
        if (!videoCodec)
        {
            m_errorString = "找不到视频编码器: " + m_config.global_effects.video_encoding.codec;
            return false;
        }

        // 创建视频流
        m_videoStream = avformat_new_stream(m_outputContext, videoCodec);
        if (!m_videoStream)
        {
            m_errorString = "创建视频流失败";
            return false;
        }

        m_videoStream->id = m_outputContext->nb_streams - 1;

        // 创建视频编码器上下文
        m_videoCodecContext = avcodec_alloc_context3(videoCodec);
        if (!m_videoCodecContext)
        {
            m_errorString = "创建视频编码器上下文失败";
            return false;
        }

        // 配置视频编码器参数
        m_videoCodecContext->width = m_config.project.width;
        m_videoCodecContext->height = m_config.project.height;
        m_videoCodecContext->time_base = {1, m_config.project.fps};
        m_videoCodecContext->framerate = {m_config.project.fps, 1};
        m_videoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;

        // 解析比特率字符串
        std::string bitrateStr = m_config.global_effects.video_encoding.bitrate;
        int bitrate = std::stoi(bitrateStr.substr(0, bitrateStr.length() - 1)) * 1000;
        m_videoCodecContext->bit_rate = bitrate;

        m_videoCodecContext->gop_size = 12;

        // 设置编码参数
        av_opt_set(m_videoCodecContext->priv_data, "preset", m_config.global_effects.video_encoding.preset.c_str(), 0);
        av_opt_set_int(m_videoCodecContext->priv_data, "crf", m_config.global_effects.video_encoding.crf, 0);

        // 打开视频编码器
        if (avcodec_open2(m_videoCodecContext, videoCodec, nullptr) < 0)
        {
            m_errorString = "打开视频编码器失败";
            return false;
        }

        // 复制参数到流
        if (avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodecContext) < 0)
        {
            m_errorString = "复制视频流参数失败";
            return false;
        }

        // 设置 stream 的 time_base 与 codec 保持一致
        m_videoStream->time_base = m_videoCodecContext->time_base;

        return true;
    }

    bool RenderEngine::createAudioStream()
    {
        // 查找音频编码器
        const AVCodec *audioCodec = avcodec_find_encoder_by_name(m_config.global_effects.audio_encoding.codec.c_str());
        if (!audioCodec)
        {
            m_errorString = "找不到音频编码器: " + m_config.global_effects.audio_encoding.codec;
            return false;
        }

        // 创建音频流
        m_audioStream = avformat_new_stream(m_outputContext, audioCodec);
        if (!m_audioStream)
        {
            m_errorString = "创建音频流失败";
            return false;
        }

        m_audioStream->id = m_outputContext->nb_streams - 1;

        // 创建音频编码器上下文
        m_audioCodecContext = avcodec_alloc_context3(audioCodec);
        if (!m_audioCodecContext)
        {
            m_errorString = "创建音频编码器上下文失败";
            return false;
        }

        // 配置音频编码器参数

        // 目标输出格式：交错 float (会在 AudioDecoder 中进行统一转换)，但编码器可能使用不同的内部格式
        m_audioCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;

        // 解析比特率字符串
        std::string bitrateStr = m_config.global_effects.audio_encoding.bitrate;
        int bitrate = std::stoi(bitrateStr.substr(0, bitrateStr.length() - 1)) * 1000;
        m_audioCodecContext->bit_rate = bitrate;

        m_audioCodecContext->sample_rate = 44100;
        // 设置通道数和布局（兼容旧/new API）
        int out_channels = m_config.global_effects.audio_encoding.channels > 0 ? m_config.global_effects.audio_encoding.channels : 2;

        // 设置 channel layout（使用 av_channel_layout_default 以兼容 ch_layout 结构）
        memset(&m_audioCodecContext->ch_layout, 0, sizeof(m_audioCodecContext->ch_layout));
#if defined(AV_CHANNEL_LAYOUT_INIT) || defined(av_channel_layout_default)
        // 如果可用，使用 av_channel_layout_default 初始化 ch_layout
        av_channel_layout_default(&m_audioCodecContext->ch_layout, out_channels);
#else
        // 回退：不设置 ch_layout，仅设置 sample_rate（某些 FFmpeg 版本使用不同字段）
        (void)out_channels;
#endif

        // 设置 time_base 为 1/sample_rate，便于时间戳换算
        m_audioCodecContext->time_base = AVRational{1, m_audioCodecContext->sample_rate};

        // 打开音频编码器
        if (avcodec_open2(m_audioCodecContext, audioCodec, nullptr) < 0)
        {
            m_errorString = "打开音频编码器失败";
            return false;
        }

        // 复制参数到流
        if (avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecContext) < 0)
        {
            m_errorString = "复制音频流参数失败";
            return false;
        }

        // 设置音频流的 time_base 与 codec context 保持一致
        m_audioStream->time_base = m_audioCodecContext->time_base;

        return true;
    }

    bool RenderEngine::renderScene(const SceneConfig &scene, double &currentTime)
    {
        std::cout << "渲染场景: " << scene.id << " (" << scene.duration << "秒)" << std::endl;

        // 如果是转场场景，暂时跳过
        if (scene.type == SceneType::TRANSITION)
        {
            std::cout << "跳过转场场景: " << scene.id << std::endl;
            return true;
        }

        int totalFrames = static_cast<int>(scene.duration * m_config.project.fps);

        // 解码图片
        ImageDecoder imageDecoder;
        if (!scene.resources.image.path.empty())
        {
            if (!imageDecoder.open(scene.resources.image.path))
            {
                std::cout << "无法打开图片: " << imageDecoder.getErrorString() << std::endl;
                // 继续生成测试帧
            }
        }

        // 解码音频
        AudioDecoder audioDecoder;
        std::vector<uint8_t> audioData;
        if (!scene.resources.audio.path.empty())
        {
            if (audioDecoder.open(scene.resources.audio.path))
            {
                audioData = audioDecoder.decode();
            }
            else
            {
                std::cout << "无法打开音频: " << audioDecoder.getErrorString() << std::endl;
            }
        }

        // 创建特效处理器
        EffectProcessor effectProcessor;
        effectProcessor.initialize(m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P);

        for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex)
        {
            // 计算进度
            double progress = static_cast<double>(frameIndex) / totalFrames;

            // 解码图片帧
            FFmpegUtils::AvFramePtr frame;
            if (imageDecoder.getWidth() > 0)
            {
                frame = imageDecoder.decode();
                if (!frame)
                {
                    // 如果解码失败，生成测试帧
                    frame = generateTestFrame(m_frameCount, m_config.project.width, m_config.project.height);
                }
            }
            else
            {
                // 生成测试帧
                frame = generateTestFrame(m_frameCount, m_config.project.width, m_config.project.height);
            }

            if (!frame)
            {
                m_errorString = "生成帧失败";
                return false;
            }

            // 应用Ken Burns特效
            if (scene.effects.ken_burns.enabled)
            {
                frame = effectProcessor.applyKenBurns(frame.get(), scene.effects.ken_burns, progress);
            }

            if (!frame)
            {
                m_errorString = "应用特效失败";
                return false;
            }

            frame->pts = m_frameCount;

            // 编码帧
            if (avcodec_send_frame(m_videoCodecContext, frame.get()) < 0)
            {
                m_errorString = "发送帧到编码器失败";
                return false;
            }

            // 接收编码后的包
            auto packet = FFmpegUtils::createAvPacket();
            while (avcodec_receive_packet(m_videoCodecContext, packet.get()) >= 0)
            {
                packet->stream_index = m_videoStream->index;
                av_packet_rescale_ts(packet.get(), m_videoCodecContext->time_base, m_videoStream->time_base);

                // 写入包
                if (av_interleaved_write_frame(m_outputContext, packet.get()) < 0)
                {
                    m_errorString = "写入视频包失败";
                    return false;
                }

                av_packet_unref(packet.get());
            }

            m_frameCount++;
            m_progress = static_cast<int>((currentTime + frameIndex / static_cast<double>(m_config.project.fps)) /
                                          (currentTime + scene.duration) * 100);

            // 每10帧输出一次进度
            if (frameIndex % 10 == 0)
            {
                std::cout << "进度: " << m_progress << "%" << std::endl;
            }
        }

        currentTime += scene.duration;
        return true;
    }

    FFmpegUtils::AvFramePtr RenderEngine::generateTestFrame(int frameIndex, int width, int height)
    {
        // 创建帧
        auto frame = FFmpegUtils::createAvFrame(width, height, AV_PIX_FMT_YUV420P);
        if (!frame)
        {
            m_errorString = "创建帧失败";
            return nullptr;
        }

        // 生成简单的测试图案
        // Y分量 (亮度)
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                // 创建渐变背景
                uint8_t Y = 128 + 64 * sin(x * 0.02 + frameIndex * 0.1) * cos(y * 0.02 + frameIndex * 0.05);
                frame->data[0][y * frame->linesize[0] + x] = Y;
            }
        }

        // U和V分量 (色度)
        for (int y = 0; y < height / 2; y++)
        {
            for (int x = 0; x < width / 2; x++)
            {
                // 创建彩色图案
                uint8_t U = 128 + 64 * sin(x * 0.04 + frameIndex * 0.08);
                uint8_t V = 128 + 64 * cos(y * 0.04 + frameIndex * 0.06);

                frame->data[1][y * frame->linesize[1] + x] = U;
                frame->data[2][y * frame->linesize[2] + x] = V;
            }
        }

        return frame;
    }

    void RenderEngine::cleanup()
    {
        if (m_videoCodecContext)
        {
            avcodec_free_context(&m_videoCodecContext);
            m_videoCodecContext = nullptr;
        }

        if (m_audioCodecContext)
        {
            avcodec_free_context(&m_audioCodecContext);
            m_audioCodecContext = nullptr;
        }

        if (m_outputContext)
        {
            if (!(m_outputContext->oformat->flags & AVFMT_NOFILE))
            {
                avio_closep(&m_outputContext->pb);
            }
            avformat_free_context(m_outputContext);
            m_outputContext = nullptr;
        }
    }

    bool RenderEngine::flushEncoder(AVCodecContext *codecCtx, AVStream *stream)
    {
        if (!codecCtx || !stream)
            return true; // nothing to flush

        int ret = avcodec_send_frame(codecCtx, nullptr);
        if (ret < 0 && ret != AVERROR_EOF)
        {
            m_errorString = "发送空帧到编码器以 flush 失败";
            return false;
        }

        auto packet = FFmpegUtils::createAvPacket();
        while (true)
        {
            ret = avcodec_receive_packet(codecCtx, packet.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                m_errorString = "从编码器接收包失败 (flush)";
                return false;
            }

            packet->stream_index = stream->index;
            av_packet_rescale_ts(packet.get(), codecCtx->time_base, stream->time_base);

            if (av_interleaved_write_frame(m_outputContext, packet.get()) < 0)
            {
                m_errorString = "写入包失败 (flush)";
                return false;
            }

            av_packet_unref(packet.get());
        }

        return true;
    }

} // namespace VideoCreator
