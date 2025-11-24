#include "RenderEngine.h"
#include "decoder/ImageDecoder.h"
#include "decoder/AudioDecoder.h"
#include "filter/EffectProcessor.h"
#include "ffmpeg_utils/AvFrameWrapper.h"
#include "ffmpeg_utils/AvPacketWrapper.h"
#include <QDebug>
#include <cmath>

namespace VideoCreator
{

    RenderEngine::RenderEngine()
        : m_outputContext(nullptr), m_videoCodecContext(nullptr), m_audioCodecContext(nullptr),
          m_videoStream(nullptr), m_audioStream(nullptr), m_frameCount(0), m_audioSamplesCount(0), m_progress(0)
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
        m_audioSamplesCount = 0;
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
            qDebug() << "音频流创建失败，继续视频渲染";
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
        for (size_t i = 0; i < m_config.scenes.size(); ++i)
        {
            const auto &currentScene = m_config.scenes[i];

            if (currentScene.type == SceneType::TRANSITION)
            {
                if (i == 0 || i >= m_config.scenes.size() - 1)
                {
                    m_errorString = "转场必须在两个场景之间";
                    return false;
                }
                const auto &fromScene = m_config.scenes[i - 1];
                const auto &toScene = m_config.scenes[i + 1];
                if (!renderTransition(currentScene, fromScene, toScene))
                {
                    return false;
                }
            }
            else
            {
                // 对于非转场场景，我们只在它不是转场的 "to" 场景时才渲染
                if (i == 0 || m_config.scenes[i - 1].type != SceneType::TRANSITION)
                {
                    if (!renderScene(currentScene))
                    {
                        return false;
                    }
                }
            }
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

        qDebug() << "视频渲染完成！总帧数: " << m_frameCount;
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

        av_channel_layout_from_mask(&m_audioCodecContext->ch_layout, AV_CH_LAYOUT_STEREO);

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

    bool RenderEngine::renderScene(const SceneConfig &scene)
    {
        qDebug() << "渲染场景: " << scene.id << " (" << scene.duration << "秒)";

        int totalFrames = static_cast<int>(scene.duration * m_config.project.fps);

        ImageDecoder imageDecoder;
        if (!scene.resources.image.path.empty())
        {
            if (!imageDecoder.open(scene.resources.image.path))
            {
                qDebug() << "无法打开图片: " << imageDecoder.getErrorString();
            }
        }

        AudioDecoder audioDecoder;
        std::vector<uint8_t> audioData;
        int audioSampleRate = 0;
        int audioChannels = 0;
        if (m_audioStream && !scene.resources.audio.path.empty())
        {
            if (audioDecoder.open(scene.resources.audio.path))
            {
                audioSampleRate = audioDecoder.getSampleRate();
                audioChannels = audioDecoder.getChannels();
                audioData = audioDecoder.decode();
            }
            else
            {
                qDebug() << "无法打开音频: " << audioDecoder.getErrorString();
            }
        }

        EffectProcessor effectProcessor;
        effectProcessor.initialize(m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P);

        size_t audioDataOffset = 0;

        for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex)
        {
            double progress = static_cast<double>(frameIndex) / totalFrames;

            FFmpegUtils::AvFramePtr videoFrame;
            if (imageDecoder.getWidth() > 0)
            {
                videoFrame = imageDecoder.decode();
                if (!videoFrame)
                {
                    videoFrame = generateTestFrame(m_frameCount, m_config.project.width, m_config.project.height);
                }
            }
            else
            {
                videoFrame = generateTestFrame(m_frameCount, m_config.project.width, m_config.project.height);
            }

            if (!videoFrame)
            {
                m_errorString = "生成视频帧失败";
                return false;
            }

            if (scene.effects.ken_burns.enabled)
            {
                videoFrame = effectProcessor.applyKenBurns(videoFrame.get(), scene.effects.ken_burns, progress);
            }

            if (!videoFrame)
            {
                m_errorString = "应用特效失败";
                return false;
            }

            videoFrame->pts = m_frameCount;

            if (avcodec_send_frame(m_videoCodecContext, videoFrame.get()) < 0)
            {
                m_errorString = "发送视频帧到编码器失败";
                return false;
            }

            auto packet = FFmpegUtils::createAvPacket();
            while (avcodec_receive_packet(m_videoCodecContext, packet.get()) >= 0)
            {
                packet->stream_index = m_videoStream->index;
                av_packet_rescale_ts(packet.get(), m_videoCodecContext->time_base, m_videoStream->time_base);
                if (av_interleaved_write_frame(m_outputContext, packet.get()) < 0)
                {
                    m_errorString = "写入视频包失败";
                    return false;
                }
                av_packet_unref(packet.get());
            }

            m_frameCount++;

            // 音频处理
            if (m_audioStream && !audioData.empty())
            {
                int samplesPerFrame = m_audioCodecContext->frame_size;
                int bytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_FLT);
                int channels = m_audioCodecContext->ch_layout.nb_channels;
                int frameBytes = samplesPerFrame * bytesPerSample * channels;

                if (scene.effects.volume_mix.enabled)
                {
                    audioData = effectProcessor.applyVolumeMix(audioData, scene.effects.volume_mix,
                                                               static_cast<double>(audioDataOffset) / audioData.size(),
                                                               audioSampleRate, audioChannels);
                }

                while (audioData.size() - audioDataOffset >= frameBytes)
                {
                    auto audioFrame = FFmpegUtils::createAvFrame();
                    audioFrame->nb_samples = samplesPerFrame;
                    audioFrame->ch_layout = m_audioCodecContext->ch_layout;
                    audioFrame->format = m_audioCodecContext->sample_fmt;
                    audioFrame->sample_rate = m_audioCodecContext->sample_rate;

                    if (av_frame_get_buffer(audioFrame.get(), 0) < 0)
                    {
                        m_errorString = "为音频帧分配缓冲区失败";
                        return false;
                    }

                    float* interleavedData = reinterpret_cast<float*>(audioData.data() + audioDataOffset);
                    for (int i = 0; i < samplesPerFrame; ++i)
                    {
                        for (int ch = 0; ch < channels; ++ch)
                        {
                            reinterpret_cast<float*>(audioFrame->data[ch])[i] = interleavedData[i * channels + ch];
                        }
                    }
                    
                    audioDataOffset += frameBytes;

                    audioFrame->pts = m_audioSamplesCount;
                    m_audioSamplesCount += audioFrame->nb_samples;

                    if (avcodec_send_frame(m_audioCodecContext, audioFrame.get()) < 0)
                    {
                        m_errorString = "发送音频帧到编码器失败";
                        return false;
                    }

                    auto audioPacket = FFmpegUtils::createAvPacket();
                    while (avcodec_receive_packet(m_audioCodecContext, audioPacket.get()) >= 0)
                    {
                        audioPacket->stream_index = m_audioStream->index;
                        av_packet_rescale_ts(audioPacket.get(), m_audioCodecContext->time_base, m_audioStream->time_base);

                        if (av_interleaved_write_frame(m_outputContext, audioPacket.get()) < 0)
                        {
                            m_errorString = "写入音频包失败";
                            return false;
                        }
                        av_packet_unref(audioPacket.get());
                    }
                }
            }
        }

        return true;
    }

    bool RenderEngine::renderTransition(const SceneConfig &transitionScene, const SceneConfig &fromScene, const SceneConfig &toScene)
    {
        qDebug() << "渲染转场: " << transitionTypeToString(transitionScene.transition_type) << " (" << transitionScene.duration << "秒)";

        int totalFrames = static_cast<int>(transitionScene.duration * m_config.project.fps);

        ImageDecoder fromDecoder, toDecoder;
        if (!fromDecoder.open(fromScene.resources.image.path) || !toDecoder.open(toScene.resources.image.path))
        {
            m_errorString = "无法打开转场中的图片";
            return false;
        }

        auto fromFrame = fromDecoder.decode();
        auto toFrame = toDecoder.decode();

        if (!fromFrame || !toFrame)
        {
            m_errorString = "解码转场帧失败";
            return false;
        }

        EffectProcessor effectProcessor;
        effectProcessor.initialize(m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P);

        for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex)
        {
            double progress = static_cast<double>(frameIndex) / totalFrames;

            FFmpegUtils::AvFramePtr blendedFrame;
            switch (transitionScene.transition_type)
            {
                case TransitionType::CROSSFADE:
                    blendedFrame = effectProcessor.applyCrossfade(fromFrame.get(), toFrame.get(), progress);
                    break;
                case TransitionType::WIPE:
                    blendedFrame = effectProcessor.applyWipe(fromFrame.get(), toFrame.get(), progress);
                    break;
                case TransitionType::SLIDE:
                    blendedFrame = effectProcessor.applySlide(fromFrame.get(), toFrame.get(), progress);
                    break;
            }

            if (!blendedFrame)
            {
                m_errorString = "应用转场特效失败";
                return false;
            }

            blendedFrame->pts = m_frameCount;

            if (avcodec_send_frame(m_videoCodecContext, blendedFrame.get()) < 0)
            {
                m_errorString = "发送转场帧到编码器失败";
                return false;
            }

            auto packet = FFmpegUtils::createAvPacket();
            while (avcodec_receive_packet(m_videoCodecContext, packet.get()) >= 0)
            {
                packet->stream_index = m_videoStream->index;
                av_packet_rescale_ts(packet.get(), m_videoCodecContext->time_base, m_videoStream->time_base);
                if (av_interleaved_write_frame(m_outputContext, packet.get()) < 0)
                {
                    m_errorString = "写入转场视频包失败";
                    return false;
                }
                av_packet_unref(packet.get());
            }

            m_frameCount++;
        }

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
