#include "ImageDecoder.h"
#include <iostream>

namespace VideoCreator
{

    ImageDecoder::ImageDecoder()
        : m_formatContext(nullptr), m_codecContext(nullptr), m_videoStreamIndex(-1),
          m_width(0), m_height(0), m_pixelFormat(AV_PIX_FMT_NONE)
    {
    }

    ImageDecoder::~ImageDecoder()
    {
        cleanup();
    }

    bool ImageDecoder::open(const std::string &filePath)
    {
        // 打开输入文件
        if (avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr) < 0)
        {
            m_errorString = "无法打开图片文件: " + filePath;
            return false;
        }

        // 查找流信息
        if (avformat_find_stream_info(m_formatContext, nullptr) < 0)
        {
            m_errorString = "无法获取流信息";
            cleanup();
            return false;
        }

        // 查找视频流
        m_videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (m_videoStreamIndex < 0)
        {
            m_errorString = "未找到视频流";
            cleanup();
            return false;
        }

        // 获取视频流
        AVStream *videoStream = m_formatContext->streams[m_videoStreamIndex];

        // 查找解码器
        const AVCodec *codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
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
        if (avcodec_parameters_to_context(m_codecContext, videoStream->codecpar) < 0)
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

        // 保存图片信息
        m_width = m_codecContext->width;
        m_height = m_codecContext->height;
        m_pixelFormat = m_codecContext->pix_fmt;

        return true;
    }

    FFmpegUtils::AvFramePtr ImageDecoder::decode()
    {
        if (!m_formatContext || !m_codecContext)
        {
            m_errorString = "解码器未打开";
            return nullptr;
        }

        AVPacket *packet = av_packet_alloc();
        if (!packet)
        {
            m_errorString = "无法分配数据包";
            return nullptr;
        }

        FFmpegUtils::AvFramePtr frame = FFmpegUtils::createAvFrame();
        if (!frame)
        {
            m_errorString = "无法创建帧";
            av_packet_free(&packet);
            return nullptr;
        }

        int response;
        while (av_read_frame(m_formatContext, packet) >= 0)
        {
            if (packet->stream_index == m_videoStreamIndex)
            {
                response = avcodec_send_packet(m_codecContext, packet);
                if (response < 0)
                {
                    m_errorString = "发送数据包到解码器失败";
                    av_packet_free(&packet);
                    return nullptr;
                }

                response = avcodec_receive_frame(m_codecContext, frame.get());
                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
                {
                    av_packet_unref(packet);
                    continue;
                }
                else if (response < 0)
                {
                    m_errorString = "从解码器接收帧失败";
                    av_packet_free(&packet);
                    return nullptr;
                }

                // 成功解码一帧
                av_packet_free(&packet);
                return frame;
            }
            av_packet_unref(packet);
        }

        // 刷新解码器
        avcodec_send_packet(m_codecContext, nullptr);
        response = avcodec_receive_frame(m_codecContext, frame.get());
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
        {
            av_packet_free(&packet);
            return nullptr;
        }
        else if (response < 0)
        {
            m_errorString = "从解码器接收帧失败";
            av_packet_free(&packet);
            return nullptr;
        }

        av_packet_free(&packet);
        return frame;
    }

    void ImageDecoder::close()
    {
        cleanup();
    }

    void ImageDecoder::cleanup()
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

        m_videoStreamIndex = -1;
        m_width = 0;
        m_height = 0;
        m_pixelFormat = AV_PIX_FMT_NONE;
    }

} // namespace VideoCreator