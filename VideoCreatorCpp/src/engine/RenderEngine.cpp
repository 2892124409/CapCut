#include "RenderEngine.h"
#include "decoder/ImageDecoder.h"
#include "decoder/AudioDecoder.h"
#include "decoder/VideoDecoder.h"
#include "filter/EffectProcessor.h"
#include "ffmpeg_utils/AvFrameWrapper.h"
#include "ffmpeg_utils/AvPacketWrapper.h"
#include <QDebug>
#include <vector>
#include <algorithm>
#include <cmath>

namespace VideoCreator
{

    // Helper to generate FFmpeg error messages
    static std::string format_ffmpeg_error(int ret, const std::string& message) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        return message + ": " + errbuf + " (code " + std::to_string(ret) + ")";
    }

    // Helper to parse bitrate strings (e.g., "5000k", "5M")
    static int64_t parseBitrate(const std::string& bitrateStr) {
        if (bitrateStr.empty()) {
            return 0;
        }
        char last_char = bitrateStr.back();
        std::string num_part = bitrateStr;
        int64_t multiplier = 1;

        if (last_char == 'k' || last_char == 'K') {
            multiplier = 1000;
            num_part.pop_back();
        } else if (last_char == 'm' || last_char == 'M') {
            multiplier = 1000000;
            num_part.pop_back();
        }

        try {
            // Trim whitespace from num_part before conversion
            size_t first = num_part.find_first_not_of(" \t\n\r");
            if (std::string::npos == first) {
                 qDebug() << "Invalid bitrate value (only whitespace): " << bitrateStr.c_str();
                 return 0;
            }
            size_t last = num_part.find_last_not_of(" \t\n\r");
            num_part = num_part.substr(first, (last - first + 1));

            return static_cast<int64_t>(std::stoll(num_part) * multiplier);
        } catch (const std::invalid_argument& e) {
            qDebug() << "Invalid bitrate value: " << bitrateStr.c_str();
            return 0;
        } catch (const std::out_of_range& e) {
            qDebug() << "Bitrate value out of range: " << bitrateStr.c_str();
            return 0;
        }
    }


    RenderEngine::RenderEngine()
        : m_videoStream(nullptr), m_audioStream(nullptr), m_audioFifo(nullptr), m_frameCount(0), m_audioSamplesCount(0), m_progress(0),
          m_totalProjectFrames(0), m_lastReportedProgress(-1), m_enableAudioTransition(false)
    {
    }

    RenderEngine::~RenderEngine()
    {
        if (m_audioFifo) {
            av_audio_fifo_free(m_audioFifo);
        }
    }

    bool RenderEngine::initialize(const ProjectConfig &config)
    {
        m_config = config;
        m_frameCount = 0;
        m_audioSamplesCount = 0;
        m_progress = 0;
        m_lastReportedProgress = -1;

        // 计算总帧数用于进度报告
        double totalDuration = 0;
        for(const auto& scene : m_config.scenes) {
            if (!scene.resources.audio.path.empty()) {
                AudioDecoder tempAudioDecoder;
                if (tempAudioDecoder.open(scene.resources.audio.path)) {
                    double audioDuration = tempAudioDecoder.getDuration();
                    if (audioDuration > 0) {
                        totalDuration += audioDuration;
                    } else {
                        totalDuration += scene.duration; // Fallback to scene duration
                    }
                    tempAudioDecoder.close();
                } else {
                    totalDuration += scene.duration; // Fallback if audio can't be opened
                }
            } else {
                totalDuration += scene.duration;
            }
        }
        m_totalProjectFrames = totalDuration * m_config.project.fps;

        if (!createOutputContext()) return false;
        if (!createVideoStream()) return false;
        if (!createAudioStream()) {
             qDebug() << "音频流创建失败，将生成无声视频";
        }

        int ret = avformat_write_header(m_outputContext.get(), nullptr);
        if (ret < 0) {
            m_errorString = format_ffmpeg_error(ret, "写入文件头失败");
            return false;
        }

        return true;
    }

    bool RenderEngine::render()
    {
        qDebug() << "开始渲染所有场景，总共" << m_config.scenes.size() << "个场景";
        
        for (size_t i = 0; i < m_config.scenes.size(); ++i)
        {
            const auto &currentScene = m_config.scenes[i];
            qDebug() << "处理场景" << i << ": ID=" << currentScene.id << ", 类型=" << (currentScene.type == SceneType::TRANSITION ? "转场" : "普通");

            if (currentScene.type == SceneType::TRANSITION)
            {
                if (i == 0 || i >= m_config.scenes.size() - 1) {
                    m_errorString = "转场必须在两个场景之间";
                    return false;
                }
                const auto &fromScene = m_config.scenes[i - 1];
                const auto &toScene = m_config.scenes[i + 1];
                if (!renderTransition(currentScene, fromScene, toScene)) return false;
            }
            else
            {
                if (!renderScene(currentScene)) return false;
            }
        }

        if (m_audioStream) {
            if (!flushAudio()) return false;
        }

        if (!flushEncoder(m_videoCodecContext.get(), m_videoStream)) return false;
        if (!flushEncoder(m_audioCodecContext.get(), m_audioStream)) return false;

        int ret = av_write_trailer(m_outputContext.get());
        if (ret < 0) {
            m_errorString = format_ffmpeg_error(ret, "写入文件尾失败");
            return false;
        }

        qDebug() << "视频渲染完成！总帧数: " << m_frameCount;
        return true;
    }

    bool RenderEngine::createOutputContext()
    {
        AVFormatContext* temp_ctx = nullptr;
        int ret = avformat_alloc_output_context2(&temp_ctx, nullptr, nullptr, m_config.project.output_path.c_str());
        if (ret < 0) {
            m_errorString = format_ffmpeg_error(ret, "创建输出上下文失败");
            return false;
        }
        m_outputContext.reset(temp_ctx);

        if (!(m_outputContext->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&m_outputContext->pb, m_config.project.output_path.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0) {
                m_errorString = format_ffmpeg_error(ret, "无法打开输出文件");
                return false;
            }
        }
        return true;
    }

    bool RenderEngine::createVideoStream()
    {
        const AVCodec *videoCodec = avcodec_find_encoder_by_name(m_config.global_effects.video_encoding.codec.c_str());
        if (!videoCodec) {
            m_errorString = "找不到视频编码器: " + m_config.global_effects.video_encoding.codec;
            return false;
        }
        m_videoStream = avformat_new_stream(m_outputContext.get(), videoCodec);
        if (!m_videoStream) {
            m_errorString = "创建视频流失败";
            return false;
        }
        m_videoStream->id = m_outputContext->nb_streams - 1;

        AVCodecContext* temp_ctx = avcodec_alloc_context3(videoCodec);
        if (!temp_ctx) {
            m_errorString = "创建视频编码器上下文失败";
            return false;
        }
        m_videoCodecContext.reset(temp_ctx);

        m_videoCodecContext->width = m_config.project.width;
        m_videoCodecContext->height = m_config.project.height;
        m_videoCodecContext->time_base = {1, m_config.project.fps};
        m_videoCodecContext->framerate = {m_config.project.fps, 1};
        m_videoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
        std::string bitrateStr = m_config.global_effects.video_encoding.bitrate;
        m_videoCodecContext->bit_rate = parseBitrate(bitrateStr);
        m_videoCodecContext->gop_size = 12;

        av_opt_set(m_videoCodecContext->priv_data, "preset", m_config.global_effects.video_encoding.preset.c_str(), 0);
        av_opt_set_int(m_videoCodecContext->priv_data, "crf", m_config.global_effects.video_encoding.crf, 0);

        int ret = avcodec_open2(m_videoCodecContext.get(), videoCodec, nullptr);
        if (ret < 0) {
            m_errorString = format_ffmpeg_error(ret, "打开视频编码器失败");
            return false;
        }

        ret = avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodecContext.get());
        if (ret < 0) {
            m_errorString = format_ffmpeg_error(ret, "复制视频流参数失败");
            return false;
        }
        m_videoStream->time_base = m_videoCodecContext->time_base;
        return true;
    }

    bool RenderEngine::createAudioStream()
    {
        const AVCodec *audioCodec = avcodec_find_encoder_by_name(m_config.global_effects.audio_encoding.codec.c_str());
        if (!audioCodec) {
            m_errorString = "找不到音频编码器: " + m_config.global_effects.audio_encoding.codec;
            return false;
        }
        m_audioStream = avformat_new_stream(m_outputContext.get(), audioCodec);
        if (!m_audioStream) {
            m_errorString = "创建音频流失败";
            return false;
        }
        m_audioStream->id = m_outputContext->nb_streams - 1;
        
        AVCodecContext* temp_ctx = avcodec_alloc_context3(audioCodec);
        if (!temp_ctx) {
            m_errorString = "创建音频编码器上下文失败";
            return false;
        }
        m_audioCodecContext.reset(temp_ctx);

        m_audioCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
        std::string bitrateStr = m_config.global_effects.audio_encoding.bitrate;
        m_audioCodecContext->bit_rate = parseBitrate(bitrateStr);
        m_audioCodecContext->sample_rate = 44100;
        av_channel_layout_from_mask(&m_audioCodecContext->ch_layout, AV_CH_LAYOUT_STEREO);
        m_audioCodecContext->time_base = {1, m_audioCodecContext->sample_rate};

        int ret = avcodec_open2(m_audioCodecContext.get(), audioCodec, nullptr);
        if (ret < 0) {
            m_errorString = format_ffmpeg_error(ret, "打开音频编码器失败");
            return false;
        }
        ret = avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecContext.get());
        if (ret < 0) {
            m_errorString = format_ffmpeg_error(ret, "复制音频流参数失败");
            return false;
        }
        m_audioFifo = av_audio_fifo_alloc(m_audioCodecContext->sample_fmt, m_audioCodecContext->ch_layout.nb_channels, 1);
        if (!m_audioFifo) {
            m_errorString = "创建音频FIFO缓冲区失败";
            return false;
        }
        m_audioStream->time_base = m_audioCodecContext->time_base;
        return true;
    }

    bool RenderEngine::renderScene(const SceneConfig &scene)
    {
        const bool isVideoScene = scene.type == SceneType::VIDEO_SCENE;

        ImageDecoder imageDecoder;
        if (!isVideoScene && !scene.resources.image.path.empty() && !imageDecoder.open(scene.resources.image.path)) {
             qDebug() << "无法打开图片: " << imageDecoder.getErrorString();
        }

        VideoDecoder videoDecoder;
        bool videoSourceAvailable = false;
        if (isVideoScene) {
            if (scene.resources.video.path.empty()) {
                m_errorString = "视频场景缺少视频文件路径";
                return false;
            }
            if (!videoDecoder.open(scene.resources.video.path)) {
                m_errorString = "无法打开视频: " + videoDecoder.getErrorString();
                return false;
            }
            videoSourceAvailable = true;
        }

        AudioDecoder audioDecoder;
        std::string resolvedAudioPath = scene.resources.audio.path;
        if (resolvedAudioPath.empty() && isVideoScene && scene.resources.video.use_audio) {
            resolvedAudioPath = scene.resources.video.path;
        }
        bool audioAvailable = m_audioStream && !resolvedAudioPath.empty() && audioDecoder.open(resolvedAudioPath);
        if (m_audioStream && !resolvedAudioPath.empty() && !audioAvailable) {
            qDebug() << "无法打开音频: " << audioDecoder.getErrorString();
        }

        if (audioAvailable) {
            if (!audioDecoder.applyVolumeEffect(scene)) {
                m_errorString = "应用音量效果失败: " + audioDecoder.getErrorString();
                return false;
            }
        }

        double sceneDuration = scene.duration;
        if (isVideoScene && videoSourceAvailable) {
            double videoDuration = videoDecoder.getDuration();
            if (videoDuration > 0) {
                sceneDuration = videoDuration;
                 qDebug() << "场景时长已同步到视频时长:" << sceneDuration << "秒";
            }
        } else if (audioAvailable) {
            double audioDuration = audioDecoder.getDuration();
            if (audioDuration > 0) {
                sceneDuration = audioDuration;
                 qDebug() << "场景时长已同步到音频时长:" << sceneDuration << "秒";
            }
        }

        int totalVideoFramesInScene = static_cast<int>(std::round(sceneDuration * m_config.project.fps));
        if (totalVideoFramesInScene <= 0) {
            qDebug() << "场景 " << scene.id << " 时长为0，跳过渲染。";
            return true;
        }

        EffectProcessor effectProcessor;
        effectProcessor.initialize(m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P, m_config.project.fps);
        
        FFmpegUtils::AvFramePtr sourceImageFrame;
        if (!isVideoScene && imageDecoder.getWidth() > 0) {
            sourceImageFrame = imageDecoder.decodeAndCache();
             if (sourceImageFrame) {
                auto scaledFrame = imageDecoder.scaleToSize(sourceImageFrame, m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P);
                sourceImageFrame = scaledFrame ? std::move(scaledFrame) : std::move(sourceImageFrame);
            }
        }
        if (!isVideoScene && !sourceImageFrame) {
             sourceImageFrame = generateTestFrame(m_frameCount, m_config.project.width, m_config.project.height);
        }

        bool kenBurnsActive = false;
        if (!isVideoScene && scene.effects.ken_burns.enabled) {
            if (!effectProcessor.startKenBurnsSequence(scene.effects.ken_burns, sourceImageFrame.get(), totalVideoFramesInScene)) {
                m_errorString = "处理Ken Burns特效序列失败: " + effectProcessor.getErrorString();
                return false;
            }
            kenBurnsActive = true;
        }
    
        int startFrameCount = m_frameCount;
        bool videoEOF = false;

        while (m_frameCount < startFrameCount + totalVideoFramesInScene)
        {
            double video_time = (double)m_frameCount / m_config.project.fps;
            double audio_time = m_audioStream ? (double)m_audioSamplesCount / m_audioCodecContext->sample_rate : video_time + 1.0; 

            if (video_time <= audio_time) {
                // --- VIDEO PART ---
                FFmpegUtils::AvFramePtr videoFrame;

                if (isVideoScene) {
                    if (videoEOF) {
                        break;
                    }
                    FFmpegUtils::AvFramePtr decodedFrame;
                    int decodeResult = videoDecoder.decodeFrame(decodedFrame);
                    if (decodeResult > 0 && decodedFrame) {
                        videoFrame = videoDecoder.scaleFrame(decodedFrame.get(), m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P);
                        if (!videoFrame) {
                            m_errorString = "缩放视频帧失败: " + videoDecoder.getErrorString();
                            return false;
                        }
                    } else if (decodeResult == 0) {
                        videoEOF = true;
                        break;
                    } else {
                        m_errorString = "解码视频帧失败: " + videoDecoder.getErrorString();
                        return false;
                    }
                } else if (kenBurnsActive) {
                    if (!effectProcessor.fetchKenBurnsFrame(videoFrame)) {
                        m_errorString = "获取Ken Burns缓存帧失败: " + effectProcessor.getErrorString();
                        return false;
                    }
                } else {
                    videoFrame = FFmpegUtils::copyAvFrame(sourceImageFrame.get());
                }

                if (!videoFrame) {
                    m_errorString = "生成或处理视频帧失败";
                    return false;
                }

                videoFrame->pts = m_frameCount;
                int ret = avcodec_send_frame(m_videoCodecContext.get(), videoFrame.get());
                if (ret < 0) {
                    m_errorString = format_ffmpeg_error(ret, "发送视频帧到编码器失败");
                    return false;
                }
                auto packet = FFmpegUtils::createAvPacket();
                while ((ret = avcodec_receive_packet(m_videoCodecContext.get(), packet.get())) == 0) {
                    packet->stream_index = m_videoStream->index;
                    av_packet_rescale_ts(packet.get(), m_videoCodecContext->time_base, m_videoStream->time_base);
                    ret = av_interleaved_write_frame(m_outputContext.get(), packet.get());
                    if (ret < 0) {
                        m_errorString = format_ffmpeg_error(ret, "写入视频包失败");
                        return false;
                    }
                    av_packet_unref(packet.get());
                }
                if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                     m_errorString = format_ffmpeg_error(ret, "从编码器接收视频包失败");
                     return false;
                }
                m_frameCount++;
                updateAndReportProgress();

            } else {
                // --- AUDIO PART ---
                if (audioAvailable) {
                    FFmpegUtils::AvFramePtr audioFrame;
                    int decode_result = audioDecoder.decodeFrame(audioFrame);
                    if (decode_result > 0 && audioFrame) {
                        if(av_audio_fifo_write(m_audioFifo, (void **)audioFrame->data, audioFrame->nb_samples) < audioFrame->nb_samples) {
                            m_errorString = "写入FIFO缓冲区失败";
                            return false;
                        }
                    } else if (decode_result == 0) {
                        audioAvailable = false;
                    } else {
                        qDebug() << "音频解码失败: " << audioDecoder.getErrorString();
                        audioAvailable = false;
                    }
                } else if (m_audioStream) {
                    const int frame_size = m_audioCodecContext->frame_size;
                    if (frame_size > 0 && av_audio_fifo_size(m_audioFifo) < frame_size) {
                        auto audioFrame = FFmpegUtils::createAvFrame();
                        audioFrame->nb_samples = frame_size;
                        audioFrame->ch_layout = m_audioCodecContext->ch_layout;
                        audioFrame->format = m_audioCodecContext->sample_fmt;
                        audioFrame->sample_rate = m_audioCodecContext->sample_rate;
                        int ret = av_frame_get_buffer(audioFrame.get(), 0);
                        if (ret < 0) {
                            m_errorString = format_ffmpeg_error(ret, "为静音帧分配缓冲区失败");
                            return false;
                        }
                        ret = av_frame_make_writable(audioFrame.get());
                        if (ret < 0) {
                            m_errorString = format_ffmpeg_error(ret, "使静音帧可写失败");
                            return false;
                        }
                        av_samples_set_silence(audioFrame->data, 0, audioFrame->nb_samples, audioFrame->ch_layout.nb_channels, (AVSampleFormat)audioFrame->format);
                        if(av_audio_fifo_write(m_audioFifo, (void**)audioFrame->data, audioFrame->nb_samples) < audioFrame->nb_samples){
                            m_errorString = "写入静音数据到FIFO失败";
                            return false;
                        }
                    }
                }
                if (!sendBufferedAudioFrames()) return false;
            }
        }
        return true;
    }

    bool RenderEngine::renderTransition(const SceneConfig &transitionScene, const SceneConfig &fromScene, const SceneConfig &toScene)
    {
        int64_t startAudioSampleCount = m_audioSamplesCount;
        int totalFrames = static_cast<int>(std::round(transitionScene.duration * m_config.project.fps));

        if (m_audioStream && m_enableAudioTransition) {
            if (!renderAudioTransition(fromScene, toScene, transitionScene.duration)) {
                return false;
            }
        }
        
        ImageDecoder fromDecoder, toDecoder;
        if (!fromDecoder.open(fromScene.resources.image.path) || !toDecoder.open(toScene.resources.image.path)) {
            m_errorString = "无法打开转场中的图片";
            return false;
        }

        // --- Determine the correct FROM frame ---
        FFmpegUtils::AvFramePtr finalFromFrame;
        if (fromScene.effects.ken_burns.enabled) {
            qDebug() << "起点场景包含Ken Burns特效，计算其最后一帧。";
            
            // 1. Get scene duration, synchronized with audio if present
            double fromSceneDuration = fromScene.duration;
            AudioDecoder tempAudioDecoder;
            if (!fromScene.resources.audio.path.empty() && tempAudioDecoder.open(fromScene.resources.audio.path)) {
                double audioDuration = tempAudioDecoder.getDuration();
                if (audioDuration > 0) {
                    fromSceneDuration = audioDuration;
                }
                tempAudioDecoder.close();
            }
            int totalFramesInFromScene = static_cast<int>(std::round(fromSceneDuration * m_config.project.fps));
            if (totalFramesInFromScene <= 0) {
                totalFramesInFromScene = 1;
            }

            // 2. Get the original, unscaled source image for the effect processor
            auto originalFromFrame = fromDecoder.decodeAndCache();
            if (!originalFromFrame) {
                m_errorString = "解码 'from' 场景的原始图片失败";
                return false;
            }

            // 3. Scale to target size/format with proper color metadata before feeding Ken Burns
            auto scaledFromFrame = fromDecoder.scaleToSize(originalFromFrame, m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P);
            if (!scaledFromFrame) {
                m_errorString = "缩放 'from' 场景图片失败，原因: " + fromDecoder.getErrorString();
                return false;
            }
            scaledFromFrame->pts = 0;

            EffectProcessor fromSceneProcessor;
            fromSceneProcessor.initialize(m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P, m_config.project.fps);
            if (!fromSceneProcessor.startKenBurnsSequence(fromScene.effects.ken_burns, scaledFromFrame.get(), totalFramesInFromScene)) {
                m_errorString = "处理 'from' 场景的 Ken Burns 特效失败: " + fromSceneProcessor.getErrorString();
                return false;
            }

            FFmpegUtils::AvFramePtr lastKbFrame;
            for (int frameIndex = 0; frameIndex < totalFramesInFromScene; ++frameIndex) {
                if (!fromSceneProcessor.fetchKenBurnsFrame(lastKbFrame)) {
                    m_errorString = "'from' 场景 Ken Burns 特效处理后未能获取最后一帧: " + fromSceneProcessor.getErrorString();
                    return false;
                }
            }
            if (!lastKbFrame) {
                m_errorString = "'from' 场景 Ken Burns 特效未生成任何帧";
                return false;
            }
            finalFromFrame = FFmpegUtils::copyAvFrame(lastKbFrame.get());
            if (!finalFromFrame) {
                m_errorString = "'from' 场景 Ken Burns 特效最后一帧复制失败";
                return false;
            }
        } else {
            qDebug() << "起点场景无特效，使用缩放后的静态图片。";
            auto fromFrame = fromDecoder.decode();
            if (!fromFrame) {
                m_errorString = "解码 'from' 帧失败";
                return false;
            }
            finalFromFrame = fromDecoder.scaleToSize(fromFrame, m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P);
        }
        
        if (!finalFromFrame) {
            m_errorString = "未能确定转场的起始帧";
            return false;
        }

        // --- Determine the correct TO frame (prefer特效首帧) ---
        FFmpegUtils::AvFramePtr scaledToFrame;
        if (toScene.effects.ken_burns.enabled) {
            // 同步音频时长
            double toSceneDuration = toScene.duration;
            AudioDecoder tempAudioDecoder;
            if (!toScene.resources.audio.path.empty() && tempAudioDecoder.open(toScene.resources.audio.path)) {
                double audioDuration = tempAudioDecoder.getDuration();
                if (audioDuration > 0) {
                    toSceneDuration = audioDuration;
                }
                tempAudioDecoder.close();
            }
            int totalFramesInToScene = static_cast<int>(std::round(toSceneDuration * m_config.project.fps));
            if (totalFramesInToScene <= 0) {
                totalFramesInToScene = 1;
            }

            auto originalToFrame = toDecoder.decode();
            if (!originalToFrame) {
                m_errorString = "解码 'to' 场景的原始图片失败";
                return false;
            }
            auto scaledSourceFrame = toDecoder.scaleToSize(originalToFrame, m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P);
            if (!scaledSourceFrame) {
                m_errorString = "缩放 'to' 场景图片失败，原因: " + toDecoder.getErrorString();
                return false;
            }
            scaledSourceFrame->pts = 0;

            EffectProcessor toSceneProcessor;
            toSceneProcessor.initialize(m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P, m_config.project.fps);
            if (!toSceneProcessor.startKenBurnsSequence(toScene.effects.ken_burns, scaledSourceFrame.get(), totalFramesInToScene)) {
                m_errorString = "处理 'to' 场景的 Ken Burns 特效失败: " + toSceneProcessor.getErrorString();
                return false;
            }

            FFmpegUtils::AvFramePtr firstKbFrame;
            if (!toSceneProcessor.fetchKenBurnsFrame(firstKbFrame)) {
                m_errorString = "'to' 场景 Ken Burns 特效处理后未能获取第一帧: " + toSceneProcessor.getErrorString();
                return false;
            }
            scaledToFrame = FFmpegUtils::copyAvFrame(firstKbFrame.get());
            if (!scaledToFrame) {
                m_errorString = "'to' 场景 Ken Burns 首帧复制失败";
                return false;
            }
        } else {
            auto toFrame = toDecoder.decode();
            if (!toFrame) {
                m_errorString = "解码 'to' 帧失败";
                return false;
            }
            scaledToFrame = toDecoder.scaleToSize(toFrame, m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P);
            if (!scaledToFrame) {
                m_errorString = "缩放 'to' 帧失败";
                return false;
            }
        }

        // --- Apply transition ---
        EffectProcessor transitionProcessor;
        transitionProcessor.initialize(m_config.project.width, m_config.project.height, AV_PIX_FMT_YUV420P, m_config.project.fps);
        if (!transitionProcessor.startTransitionSequence(transitionScene.transition_type, finalFromFrame.get(), scaledToFrame.get(), totalFrames)) {
            m_errorString = "应用转场特效失败: " + transitionProcessor.getErrorString();
            return false;
        }

        for (int frameIndex = 0; frameIndex < totalFrames; ++frameIndex)
        {
            FFmpegUtils::AvFramePtr blendedFrame;
            if (!transitionProcessor.fetchTransitionFrame(blendedFrame)) {
                m_errorString = "应用转场特效失败: " + transitionProcessor.getErrorString();
                return false;
            }
            blendedFrame->pts = m_frameCount;
            int ret = avcodec_send_frame(m_videoCodecContext.get(), blendedFrame.get());
            if (ret < 0) {
                m_errorString = format_ffmpeg_error(ret, "发送转场帧到编码器失败");
                return false;
            }
            auto packet = FFmpegUtils::createAvPacket();
            while ((ret = avcodec_receive_packet(m_videoCodecContext.get(), packet.get())) >= 0) {
                packet->stream_index = m_videoStream->index;
                av_packet_rescale_ts(packet.get(), m_videoCodecContext->time_base, m_videoStream->time_base);
                ret = av_interleaved_write_frame(m_outputContext.get(), packet.get());
                if (ret < 0) {
                    m_errorString = format_ffmpeg_error(ret, "写入转场视频包失败");
                    return false;
                }
                av_packet_unref(packet.get());
            }
            if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                m_errorString = format_ffmpeg_error(ret, "从编码器接收转场包失败");
                return false;
            }

            if (m_audioStream) {
                double video_time_in_scene = (double)(frameIndex + 1) / m_config.project.fps;
                double audio_time_in_scene = (double)(m_audioSamplesCount - startAudioSampleCount) / m_audioCodecContext->sample_rate;
                while(audio_time_in_scene < video_time_in_scene) {
                    const int frame_size = m_audioCodecContext->frame_size;
                    if (frame_size <= 0) break;
                    
                    auto audioFrame = FFmpegUtils::createAvFrame();
                    audioFrame->nb_samples = frame_size;
                    audioFrame->ch_layout = m_audioCodecContext->ch_layout;
                    audioFrame->format = m_audioCodecContext->sample_fmt;
                    audioFrame->sample_rate = m_audioCodecContext->sample_rate;

                    int ret = av_frame_get_buffer(audioFrame.get(), 0);
                    if (ret < 0) {
                         m_errorString = format_ffmpeg_error(ret, "为静音帧分配缓冲区失败 (Transition)");
                         return false;
                    }
                    ret = av_frame_make_writable(audioFrame.get());
                     if (ret < 0) {
                        m_errorString = format_ffmpeg_error(ret, "使静音帧可写失败 (Transition)");
                        return false;
                    }
                    av_samples_set_silence(audioFrame->data, 0, audioFrame->nb_samples, audioFrame->ch_layout.nb_channels, (AVSampleFormat)audioFrame->format);

                    if(av_audio_fifo_write(m_audioFifo, (void**)audioFrame->data, audioFrame->nb_samples) < audioFrame->nb_samples){
                        m_errorString = "写入静音数据到FIFO失败 (Transition)";
                        return false;
                    }

                    if (!sendBufferedAudioFrames()) return false;
                     audio_time_in_scene = (double)(m_audioSamplesCount - startAudioSampleCount) / m_audioCodecContext->sample_rate;
                }
            }
            m_frameCount++;
            updateAndReportProgress();
        }
        return true;
    }

    bool RenderEngine::renderAudioTransition(const SceneConfig &fromScene, const SceneConfig &toScene, double duration_seconds)
    {
        if (!m_audioStream || !m_audioCodecContext || duration_seconds <= 0.0) return true;

        const int sample_rate = m_audioCodecContext->sample_rate > 0 ? m_audioCodecContext->sample_rate : 44100;
        int frame_size = m_audioCodecContext->frame_size;
        if (frame_size <= 0) frame_size = 1024; // 合理的默认值

        const int total_samples = static_cast<int>(std::ceil(duration_seconds * sample_rate));
        const double vol_from = fromScene.resources.audio.volume <= 0 ? 0.0 : fromScene.resources.audio.volume;
        const double vol_to = toScene.resources.audio.volume <= 0 ? 0.0 : toScene.resources.audio.volume;

        AudioDecoder fromDecoder;
        AudioDecoder toDecoder;
        bool fromAvailable = !fromScene.resources.audio.path.empty() && fromDecoder.open(fromScene.resources.audio.path);
        bool toAvailable = !toScene.resources.audio.path.empty() && toDecoder.open(toScene.resources.audio.path);

        if (fromAvailable) {
            if (!fromDecoder.applyVolumeEffect(fromScene)) {
                qDebug() << "起始场景音量特效应用失败，继续使用原始音频。原因: " << fromDecoder.getErrorString().c_str();
            }
        }
        if (toAvailable) {
            if (!toDecoder.applyVolumeEffect(toScene)) {
                qDebug() << "目标场景音量特效应用失败，继续使用原始音频。原因: " << toDecoder.getErrorString().c_str();
            }
        }

        // 如果没有可用音频源，则保持旧逻辑由后续循环填充静音
        if (!fromAvailable && !toAvailable) {
            return true;
        }

        if (fromAvailable) {
            double fromDuration = fromDecoder.getDuration();
            if (fromDuration <= 0) {
                fromDuration = fromScene.duration;
            }
            double start_time = std::max(0.0, fromDuration - duration_seconds);
            fromDecoder.seek(start_time);
        }

        struct AudioBuffer {
            std::vector<float> channels[2];
            size_t readPos = 0;
            bool exhausted = false;
        };

        AudioBuffer fromBuf;
        AudioBuffer toBuf;

        auto compactBuffer = [](AudioBuffer &buf) {
            const size_t threshold = 8192;
            if (buf.readPos > threshold) {
                for (auto &ch : buf.channels) {
                    if (buf.readPos <= ch.size()) {
                        ch.erase(ch.begin(), ch.begin() + static_cast<long long>(buf.readPos));
                    } else {
                        ch.clear();
                    }
                }
                buf.readPos = 0;
            }
        };

        auto ensureSamples = [this](AudioDecoder &decoder, AudioBuffer &buf, int needed, bool &available) -> bool {
            while (!buf.exhausted && static_cast<int>(buf.channels[0].size() - buf.readPos) < needed) {
                FFmpegUtils::AvFramePtr frame;
                int ret = decoder.decodeFrame(frame);
                if (ret > 0 && frame) {
                    const int nb = frame->nb_samples;
                    int ch = frame->ch_layout.nb_channels > 0 ? frame->ch_layout.nb_channels : 1;
                    ch = std::min(ch, 2); // 仅处理前两个声道
                    for (int c = 0; c < ch; ++c) {
                        float *data = reinterpret_cast<float *>(frame->data[c]);
                        buf.channels[c].insert(buf.channels[c].end(), data, data + nb);
                    }
                    // 单声道时复制到右声道，保证双声道输出
                    if (ch == 1) {
                        buf.channels[1].insert(buf.channels[1].end(), buf.channels[0].end() - nb, buf.channels[0].end());
                    }
                    // 对齐两个声道长度
                    const size_t max_len = std::max(buf.channels[0].size(), buf.channels[1].size());
                    buf.channels[0].resize(max_len, 0.0f);
                    buf.channels[1].resize(max_len, 0.0f);
                }
                else if (ret == 0) {
                    buf.exhausted = true;
                    break;
                }
                else {
                    buf.exhausted = true;
                    available = false;
                    qDebug() << "音频转场解码失败，使用静音代替。";
                    break;
                }
            }
            return true;
        };

        int processed = 0;
        while (processed < total_samples) {
            int chunk = std::min(frame_size, total_samples - processed);

            if (fromAvailable) {
                if (!ensureSamples(fromDecoder, fromBuf, chunk, fromAvailable)) return false;
            }
            if (toAvailable) {
                if (!ensureSamples(toDecoder, toBuf, chunk, toAvailable)) return false;
            }

            auto mixedFrame = FFmpegUtils::createAvFrame();
            mixedFrame->nb_samples = chunk;
            mixedFrame->ch_layout = m_audioCodecContext->ch_layout;
            mixedFrame->format = m_audioCodecContext->sample_fmt;
            mixedFrame->sample_rate = sample_rate;
            int ret = av_frame_get_buffer(mixedFrame.get(), 0);
            if (ret < 0) {
                m_errorString = format_ffmpeg_error(ret, "为转场混音帧分配缓冲区失败");
                return false;
            }

            const int channels = m_audioCodecContext->ch_layout.nb_channels > 0 ? m_audioCodecContext->ch_layout.nb_channels : 2;
            for (int c = 0; c < channels; ++c) {
                float *dst = reinterpret_cast<float *>(mixedFrame->data[c]);
                for (int i = 0; i < chunk; ++i) {
                    float s_from = 0.0f;
                    float s_to = 0.0f;
                    if (fromAvailable && fromBuf.readPos + i < fromBuf.channels[c % 2].size()) {
                        s_from = fromBuf.channels[c % 2][fromBuf.readPos + i];
                    }
                    if (toAvailable && toBuf.readPos + i < toBuf.channels[c % 2].size()) {
                        s_to = toBuf.channels[c % 2][toBuf.readPos + i];
                    }
                    double t = static_cast<double>(processed + i) / static_cast<double>(total_samples);
                    double w_from = 1.0 - t;
                    double w_to = t;
                    dst[i] = static_cast<float>(s_from * w_from * vol_from + s_to * w_to * vol_to);
                }
            }

            fromBuf.readPos += chunk;
            toBuf.readPos += chunk;
            compactBuffer(fromBuf);
            compactBuffer(toBuf);

            if (av_audio_fifo_write(m_audioFifo, (void **)mixedFrame->data, mixedFrame->nb_samples) < mixedFrame->nb_samples) {
                m_errorString = "写入转场混音数据到FIFO失败";
                return false;
            }

            if (!sendBufferedAudioFrames()) return false;
            processed += chunk;
        }

        return true;
    }

    FFmpegUtils::AvFramePtr RenderEngine::generateTestFrame(int frameIndex, int width, int height)
    {
        auto frame = FFmpegUtils::createAvFrame(width, height, AV_PIX_FMT_YUV420P);
        if (!frame) {
            m_errorString = "创建帧失败";
            return nullptr;
        }
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = 128 + 64 * sin(x * 0.02 + frameIndex * 0.1) * cos(y * 0.02 + frameIndex * 0.05);
            }
        }
        for (int y = 0; y < height / 2; y++) {
            for (int x = 0; x < width / 2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + 64 * sin(x * 0.04 + frameIndex * 0.08);
                frame->data[2][y * frame->linesize[2] + x] = 128 + 64 * cos(y * 0.04 + frameIndex * 0.06);
            }
        }
        return frame;
    }
    
    bool RenderEngine::sendBufferedAudioFrames()
    {
        if (!m_audioFifo || !m_audioCodecContext) return true; // Return true if no audio configured
        const int frame_size = m_audioCodecContext->frame_size;
        if (frame_size <= 0) return true;

        while (av_audio_fifo_size(m_audioFifo) >= frame_size)
        {
            auto frame = FFmpegUtils::createAvFrame();
            frame->nb_samples = frame_size;
            frame->ch_layout = m_audioCodecContext->ch_layout;
            frame->format = m_audioCodecContext->sample_fmt;
            frame->sample_rate = m_audioCodecContext->sample_rate;
            int ret = av_frame_get_buffer(frame.get(), 0);
            if (ret < 0) {
                m_errorString = format_ffmpeg_error(ret, "为音频帧分配缓冲区失败 (FIFO)");
                return false;
            }
            if (av_audio_fifo_read(m_audioFifo, (void**)frame->data, frame_size) < 0) {
                m_errorString = "从FIFO读取音频数据失败";
                return false;
            }
            frame->pts = m_audioSamplesCount;
            m_audioSamplesCount += frame->nb_samples;
            ret = avcodec_send_frame(m_audioCodecContext.get(), frame.get());
            if (ret < 0) {
                m_errorString = format_ffmpeg_error(ret, "发送音频帧到编码器失败 (FIFO)");
                return false;
            }
            auto packet = FFmpegUtils::createAvPacket();
            while ((ret = avcodec_receive_packet(m_audioCodecContext.get(), packet.get())) == 0) {
                packet->stream_index = m_audioStream->index;
                av_packet_rescale_ts(packet.get(), m_audioCodecContext->time_base, m_audioStream->time_base);
                ret = av_interleaved_write_frame(m_outputContext.get(), packet.get());
                if (ret < 0) {
                    m_errorString = format_ffmpeg_error(ret, "写入音频包失败 (FIFO)");
                    return false;
                }
                av_packet_unref(packet.get());
            }
             if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                m_errorString = format_ffmpeg_error(ret, "从编码器接收音频包失败 (FIFO)");
                return false;
            }
        }
        return true;
    }

    bool RenderEngine::flushAudio()
    {
        if (!m_audioFifo || !m_audioCodecContext) return true;
        const int frame_size = m_audioCodecContext->frame_size;
        if (frame_size <= 0) return true;
        const int remaining_samples = av_audio_fifo_size(m_audioFifo);
        if (remaining_samples > 0) {
            const int silence_to_add = frame_size - remaining_samples;
            auto silenceFrame = FFmpegUtils::createAvFrame();
            silenceFrame->nb_samples = silence_to_add;
            silenceFrame->ch_layout = m_audioCodecContext->ch_layout;
            silenceFrame->format = m_audioCodecContext->sample_fmt;
            silenceFrame->sample_rate = m_audioCodecContext->sample_rate;
            int ret = av_frame_get_buffer(silenceFrame.get(), 0);
            if (ret < 0) {
                m_errorString = format_ffmpeg_error(ret, "为静音帧分配缓冲区失败 (Flush)");
                return false;
            }
            ret = av_frame_make_writable(silenceFrame.get());
            if(ret < 0) {
                 m_errorString = format_ffmpeg_error(ret, "使静音帧可写失败 (Flush)");
                 return false;
            }
            av_samples_set_silence(silenceFrame->data, 0, silence_to_add, silenceFrame->ch_layout.nb_channels, (AVSampleFormat)silenceFrame->format);
            av_audio_fifo_write(m_audioFifo, (void**)silenceFrame->data, silence_to_add);
        }
        return sendBufferedAudioFrames();
    }



    bool RenderEngine::flushEncoder(AVCodecContext *codecCtx, AVStream *stream)
    {
        if (!codecCtx || !stream) return true;
        int ret = avcodec_send_frame(codecCtx, nullptr);
        if (ret < 0 && ret != AVERROR_EOF) {
            m_errorString = format_ffmpeg_error(ret, "发送空帧到编码器以 flush 失败");
            return false;
        }
        auto packet = FFmpegUtils::createAvPacket();
        while (true)
        {
            ret = avcodec_receive_packet(codecCtx, packet.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) {
                m_errorString = format_ffmpeg_error(ret, "从编码器接收包失败 (flush)");
                return false;
            }
            packet->stream_index = stream->index;
            av_packet_rescale_ts(packet.get(), codecCtx->time_base, stream->time_base);
            ret = av_interleaved_write_frame(m_outputContext.get(), packet.get());
            if (ret < 0) {
                m_errorString = format_ffmpeg_error(ret, "写入包失败 (flush)");
                return false;
            }
            av_packet_unref(packet.get());
        }
        return true;
    }

    void RenderEngine::updateAndReportProgress()
    {
        if (m_totalProjectFrames > 0) {
            m_progress = static_cast<int>((m_frameCount / m_totalProjectFrames) * 100);
            if (m_progress > m_lastReportedProgress) {
                qDebug() << "合成进度: " << m_progress << "%";
                m_lastReportedProgress = m_progress;
            }
        }
    }

} // namespace VideoCreator
