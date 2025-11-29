#include "ImageDecoder.h"
#include <iostream>
#include <QDebug>

namespace VideoCreator
{

    ImageDecoder::ImageDecoder()
        : m_formatContext(nullptr), m_codecContext(nullptr), m_videoStreamIndex(-1),
          m_width(0), m_height(0), m_pixelFormat(AV_PIX_FMT_NONE), m_swsContext(nullptr), m_cachedFrame(nullptr)
    {
    }

    ImageDecoder::~ImageDecoder()
    {
        cleanup();
    }

    bool ImageDecoder::open(const std::string &filePath)
    {
        qDebug() << "打开图片文件: " << filePath.c_str();
        
        // 打开输入文件
        if (avformat_open_input(&m_formatContext, filePath.c_str(), nullptr, nullptr) < 0)
        {
            m_errorString = "无法打开图片文件: " + filePath;
            qDebug() << "打开图片文件失败: " << m_errorString.c_str();
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

        qDebug() << "图片解码器初始化成功 - 尺寸: " << m_width << "x" << m_height << " 格式: " << m_pixelFormat;

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
                qDebug() << "成功解码图片帧 - 尺寸: " << frame->width << "x" << frame->height << " 格式: " << frame->format;
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
            qDebug() << "图片解码器刷新完成，无更多帧";
            return nullptr;
        }
        else if (response < 0)
        {
            m_errorString = "从解码器接收帧失败";
            av_packet_free(&packet);
            return nullptr;
        }

        qDebug() << "成功解码图片帧 (刷新) - 尺寸: " << frame->width << "x" << frame->height << " 格式: " << frame->format;
        av_packet_free(&packet);
        return frame;
    }

    FFmpegUtils::AvFramePtr ImageDecoder::decodeAndCache()
    {
        // 缓存第一帧，避免重复解码
        if (m_cachedFrame) {
            qDebug() << "使用缓存的图片帧";
            return FFmpegUtils::copyAvFrame(m_cachedFrame.get());
        }
        
        m_cachedFrame = decode();
        return FFmpegUtils::copyAvFrame(m_cachedFrame.get());
    }
    
    void ImageDecoder::close()
    {
        cleanup();
    }
    
    FFmpegUtils::AvFramePtr ImageDecoder::scaleToSize(FFmpegUtils::AvFramePtr& frame, int targetWidth, int targetHeight, AVPixelFormat targetFormat)
    {
        if (!frame)
        {
            m_errorString = "输入帧为空";
            return nullptr;
        }
    
        // 创建缩放上下文
        m_swsContext = sws_getCachedContext(m_swsContext,
                                           frame->width, frame->height, (AVPixelFormat)frame->format,
                                           targetWidth, targetHeight, targetFormat,
                                           SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_swsContext)
        {
            m_errorString = "无法创建缩放上下文";
            return nullptr;
        }
    
        // --- 开始颜色空间修复 ---
        // 确定源色彩范围 (0 for limited/MPEG, 1 for full/JPEG)
        int srcRange = (frame->color_range == AVCOL_RANGE_MPEG) ? 0 : 1;
        // 目标色彩范围总是 limited range for video pipeline
        int dstRange = 0;
    
        // 获取色彩矩阵系数, 基于色彩空间 (e.g., BT.709, BT.601)
        int colorspace = frame->colorspace;
        if (colorspace == AVCOL_SPC_UNSPECIFIED) {
            colorspace = (frame->height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        }
        const int *coeffs = sws_getCoefficients(colorspace);
    
        // 设置色彩空间和范围转换细节
        sws_setColorspaceDetails(m_swsContext, coeffs, srcRange, coeffs, dstRange, 0, 0, 0);
        // --- 结束颜色空间修复 ---
    
        // 创建目标帧
        auto scaledFrame = FFmpegUtils::createAvFrame(targetWidth, targetHeight, targetFormat);
        if (!scaledFrame)
        {
            m_errorString = "无法创建缩放后的帧";
            return nullptr;
        }
    
        // 执行缩放
        int result = sws_scale(m_swsContext,
                              frame->data, frame->linesize,
                              0, frame->height,
                              scaledFrame->data, scaledFrame->linesize);
        if (result <= 0)
        {
            m_errorString = "缩放失败";
            return nullptr;
        }
    
        // 在输出帧上设置正确的色彩信息
        scaledFrame->colorspace = static_cast<AVColorSpace>(colorspace);
        scaledFrame->color_range = AVCOL_RANGE_MPEG;
        scaledFrame->color_primaries = (frame->height >= 720) ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
        scaledFrame->color_trc = (frame->height >= 720) ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
        scaledFrame->sample_aspect_ratio = AVRational{1, 1};
    
        return scaledFrame;
    }    
    void ImageDecoder::cleanup()
    {
        m_cachedFrame.reset(); // 清除缓存
    
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
        m_width = 0;
        m_height = 0;
        m_pixelFormat = AV_PIX_FMT_NONE;
    }
} // namespace VideoCreator
