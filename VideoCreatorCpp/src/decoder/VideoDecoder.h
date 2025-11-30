#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <string>
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"

namespace VideoCreator
{

    class VideoDecoder
    {
    public:
        VideoDecoder();
        ~VideoDecoder();

        bool open(const std::string &filePath);

        // 解码下一帧原始画面
        int decodeFrame(FFmpegUtils::AvFramePtr &frame);

        // 将帧缩放/转换成目标尺寸与像素格式
        FFmpegUtils::AvFramePtr scaleFrame(const AVFrame *frame, int targetWidth, int targetHeight, AVPixelFormat targetFormat = AV_PIX_FMT_YUV420P);

        double getDuration() const;
        double getFrameRate() const { return m_frameRate; }
        void close();
        std::string getErrorString() const { return m_errorString; }

        int sourceWidth() const { return m_codecContext ? m_codecContext->width : 0; }
        int sourceHeight() const { return m_codecContext ? m_codecContext->height : 0; }
        AVPixelFormat sourceFormat() const { return m_codecContext ? m_codecContext->pix_fmt : AV_PIX_FMT_NONE; }

    private:
        AVFormatContext *m_formatContext;
        AVCodecContext *m_codecContext;
        SwsContext *m_swsContext;
        int m_videoStreamIndex;
        AVRational m_timeBase;
        double m_frameRate;
        int64_t m_duration;

        std::string m_errorString;

        void cleanup();
    };

} // namespace VideoCreator

#endif // VIDEO_DECODER_H
