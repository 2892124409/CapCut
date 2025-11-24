#ifndef IMAGE_DECODER_H
#define IMAGE_DECODER_H

#include <string>
#include <memory>
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"

namespace VideoCreator
{

    class ImageDecoder
    {
    public:
        ImageDecoder();
        ~ImageDecoder();

        // 打开图片文件
        bool open(const std::string &filePath);

        // 解码图片
        FFmpegUtils::AvFramePtr decode();

        // 解码并缓存图片（避免重复解码）
        FFmpegUtils::AvFramePtr decodeAndCache();

        // 缩放图片到指定尺寸
        FFmpegUtils::AvFramePtr scaleToSize(FFmpegUtils::AvFramePtr& frame, int targetWidth, int targetHeight, AVPixelFormat targetFormat = AV_PIX_FMT_YUV420P);

        // 获取图片信息
        int getWidth() const { return m_width; }
        int getHeight() const { return m_height; }
        AVPixelFormat getPixelFormat() const { return m_pixelFormat; }

        // 关闭解码器
        void close();

        // 获取错误信息
        std::string getErrorString() const { return m_errorString; }

    private:
        // FFmpeg资源
        AVFormatContext *m_formatContext;
        AVCodecContext *m_codecContext;
        int m_videoStreamIndex;
        SwsContext *m_swsContext;

        // 图片信息
        int m_width;
        int m_height;
        AVPixelFormat m_pixelFormat;

        std::string m_errorString;
        FFmpegUtils::AvFramePtr m_cachedFrame;

        // 清理资源
        void cleanup();
    };

} // namespace VideoCreator

#endif // IMAGE_DECODER_H
