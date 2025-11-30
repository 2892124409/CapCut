#include "VideoDecoder.h"
#include <QDebug>
#include "ffmpeg_utils/AvPacketWrapper.h"

namespace VideoCreator
{

    VideoDecoder::VideoDecoder()
        : m_formatContext(nullptr), m_codecContext(nullptr), m_swsContext(nullptr),
          m_videoStreamIndex(-1), m_timeBase{1, 1}, m_frameRate(0.0), m_duration(0)
    {
    }

    VideoDecoder::~VideoDecoder()
    {
        cleanup();
    }

    bool VideoDecoder::open(const std::string &filePath)
    {
        if (avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr) < 0)
        {
            m_errorString = "无法打开视频文件: " + filePath;
            return false;
        }

        if (avformat_find_stream_info(m_formatContext, nullptr) < 0)
        {
            m_errorString = "无法获取视频流信息";
            cleanup();
            return false;
        }

        m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (m_videoStreamIndex < 0)
        {
            m_errorString = "未找到视频流";
            cleanup();
            return false;
        }

        AVStream *videoStream = m_formatContext->streams[m_videoStreamIndex];
        const AVCodec *codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
        if (!codec)
        {
            m_errorString = "未找到视频解码器";
            cleanup();
            return false;
        }

        m_codecContext = avcodec_alloc_context3(codec);
        if (!m_codecContext)
        {
            m_errorString = "无法创建视频解码器上下文";
            cleanup();
            return false;
        }

        if (avcodec_parameters_to_context(m_codecContext, videoStream->codecpar) < 0)
        {
            m_errorString = "无法复制视频参数";
            cleanup();
            return false;
        }

        if (avcodec_open2(m_codecContext, codec, nullptr) < 0)
        {
            m_errorString = "无法打开视频解码器";
            cleanup();
            return false;
        }

        m_timeBase = videoStream->time_base;
        if (videoStream->duration != AV_NOPTS_VALUE)
        {
            m_duration = videoStream->duration;
        }
        else
        {
            m_duration = m_formatContext->duration;
        }

        AVRational guess = av_guess_frame_rate(m_formatContext, videoStream, nullptr);
        if (guess.num > 0 && guess.den > 0)
        {
            m_frameRate = av_q2d(guess);
        }
        else if (videoStream->avg_frame_rate.num > 0 && videoStream->avg_frame_rate.den > 0)
        {
            m_frameRate = av_q2d(videoStream->avg_frame_rate);
        }
        else
        {
            m_frameRate = 0.0;
        }

        return true;
    }

    int VideoDecoder::decodeFrame(FFmpegUtils::AvFramePtr &frame)
    {
        if (!m_formatContext || !m_codecContext)
        {
            m_errorString = "视频解码器未初始化";
            return -1;
        }

        auto packet = FFmpegUtils::createAvPacket();
        auto rawFrame = FFmpegUtils::createAvFrame();
        int ret = 0;

        while (true)
        {
            ret = avcodec_receive_frame(m_codecContext, rawFrame.get());
            if (ret == 0)
            {
                frame = FFmpegUtils::copyAvFrame(rawFrame.get());
                return frame ? 1 : -1;
            }
            if (ret == AVERROR_EOF)
            {
                return 0;
            }
            if (ret != AVERROR(EAGAIN))
            {
                m_errorString = "从视频解码器获取帧失败";
                return -1;
            }

            while (true)
            {
                ret = av_read_frame(m_formatContext, packet.get());
                if (ret < 0)
                {
                    avcodec_send_packet(m_codecContext, nullptr);
                    break;
                }

                if (packet->stream_index == m_videoStreamIndex)
                {
                    ret = avcodec_send_packet(m_codecContext, packet.get());
                    av_packet_unref(packet.get());
                    if (ret < 0)
                    {
                        m_errorString = "发送视频包失败";
                        return -1;
                    }
                    break;
                }
                av_packet_unref(packet.get());
            }
        }
    }

    FFmpegUtils::AvFramePtr VideoDecoder::scaleFrame(const AVFrame *frame, int targetWidth, int targetHeight, AVPixelFormat targetFormat)
    {
        if (!frame)
        {
            m_errorString = "源视频帧为空";
            return nullptr;
        }

        m_swsContext = sws_getCachedContext(m_swsContext,
                                            frame->width, frame->height, (AVPixelFormat)frame->format,
                                            targetWidth, targetHeight, targetFormat,
                                            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_swsContext)
        {
            m_errorString = "创建视频缩放上下文失败";
            return nullptr;
        }

        int srcRange = (frame->color_range == AVCOL_RANGE_MPEG) ? 0 : 1;
        int dstRange = 0;
        int colorspace = frame->colorspace;
        if (colorspace == AVCOL_SPC_UNSPECIFIED)
        {
            colorspace = (frame->height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        }
        const int *coeffs = sws_getCoefficients(colorspace);
        sws_setColorspaceDetails(m_swsContext, coeffs, srcRange, coeffs, dstRange, 0, 0, 0);

        auto scaledFrame = FFmpegUtils::createAvFrame(targetWidth, targetHeight, targetFormat);
        if (!scaledFrame)
        {
            m_errorString = "创建缩放后的帧失败";
            return nullptr;
        }

        int result = sws_scale(m_swsContext,
                               frame->data, frame->linesize,
                               0, frame->height,
                               scaledFrame->data, scaledFrame->linesize);
        if (result <= 0)
        {
            m_errorString = "视频缩放失败";
            return nullptr;
        }

        scaledFrame->colorspace = static_cast<AVColorSpace>(colorspace);
        scaledFrame->color_range = AVCOL_RANGE_MPEG;
        scaledFrame->color_primaries = (frame->height >= 720) ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
        scaledFrame->color_trc = (frame->height >= 720) ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
        scaledFrame->sample_aspect_ratio = AVRational{1, 1};

        return scaledFrame;
    }

    double VideoDecoder::getDuration() const
    {
        if (!m_formatContext || m_videoStreamIndex < 0)
        {
            return 0.0;
        }

        if (m_duration != AV_NOPTS_VALUE)
        {
            return m_duration * av_q2d(m_timeBase);
        }

        return 0.0;
    }

    void VideoDecoder::close()
    {
        cleanup();
    }

    void VideoDecoder::cleanup()
    {
        if (m_swsContext)
        {
            sws_freeContext(m_swsContext);
            m_swsContext = nullptr;
        }
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
        m_videoStreamIndex = -1;
        m_duration = 0;
    }

} // namespace VideoCreator
