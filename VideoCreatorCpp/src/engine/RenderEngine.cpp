#include "RenderEngine.h"
#include "ffmpeg_utils/AvFrameWrapper.h"
#include "ffmpeg_utils/AvPacketWrapper.h"
#include <iostream>
#include <cmath>

namespace VideoCreator
{

    RenderEngine::RenderEngine()
        : m_outputContext(nullptr), m_videoCodecContext(nullptr), m_audioCodecContext(nullptr), m_videoStream(nullptr), m_audioStream(nullptr), m_frameCount(0), m_progress(0)
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
                                           m_config.output.output_path.c_str()) < 0)
        {
            m_errorString = "创建输出上下文失败";
            return false;
        }

        if (!m_outputContext)
        {
            m_errorString = "输出上下文为空";
            return false;
        }

        return true;
    }

    bool RenderEngine::createVideoStream()
    {
        // 查找视频编码器
        const AVCodec *videoCodec = avcodec_find_encoder_by_name(m_config.output.video_codec.c_str());
        if (!videoCodec)
        {
            m_errorString = "找不到视频编码器: " + m_config.output.video_codec;
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
        m_videoCodecContext->width = m_config.output.width;
        m_videoCodecContext->height = m_config.output.height;
        m_videoCodecContext->time_base = {1, m_config.output.frame_rate};
        m_videoCodecContext->framerate = {m_config.output.frame_rate, 1};
        m_videoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
        m_videoCodecContext->bit_rate = m_config.output.video_bitrate;
        m_videoCodecContext->gop_size = 12;

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

        return true;
    }

    bool RenderEngine::createAudioStream()
    {
        // 查找音频编码器
        const AVCodec *audioCodec = avcodec_find_encoder_by_name(m_config.output.audio_codec.c_str());
        if (!audioCodec)
        {
            m_errorString = "找不到音频编码器: " + m_config.output.audio_codec;
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
        m_audioCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
        m_audioCodecContext->bit_rate = m_config.output.audio_bitrate;
        m_audioCodecContext->sample_rate = 44100;
        // 使用新的API设置声道布局
        m_audioCodecContext->ch_layout = AV_CHANNEL_LAYOUT_STEREO;

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

        return true;
    }

    bool RenderEngine::renderScene(const SceneConfig &scene, double &currentTime)
    {
        std::cout << "渲染场景: " << scene.name << " (" << scene.duration << "秒)" << std::endl;

        int totalFrames = static_cast<int>(scene.duration * m_config.output.frame_rate);

        for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex)
        {
            // 生成测试帧
            if (!generateTestFrame(m_frameCount, m_config.output.width, m_config.output.height))
            {
                return false;
            }

            m_frameCount++;
            m_progress = static_cast<int>((currentTime + frameIndex / static_cast<double>(m_config.output.frame_rate)) /
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

    bool RenderEngine::generateTestFrame(int frameIndex, int width, int height)
    {
        // 创建帧
        auto frame = FFmpegUtils::createAvFrame(width, height, AV_PIX_FMT_YUV420P);
        if (!frame)
        {
            m_errorString = "创建帧失败";
            return false;
        }

        frame->pts = m_frameCount;

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

        return true;
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

} // namespace VideoCreator
