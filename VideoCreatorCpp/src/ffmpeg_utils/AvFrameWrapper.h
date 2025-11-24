#ifndef AV_FRAME_WRAPPER_H
#define AV_FRAME_WRAPPER_H

#include <memory>
#include "FFmpegHeaders.h"

namespace FFmpegUtils {

// AVFrame 自定义删除器
struct AvFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) {
            av_frame_free(&frame);
        }
    }
};

// AVFrame 智能指针
using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;

// 创建 AVFrame 智能指针
inline AvFramePtr createAvFrame() {
    AVFrame* frame = av_frame_alloc();
    return AvFramePtr(frame);
}

// 复制 AVFrame
inline AvFramePtr copyAvFrame(const AVFrame* src) {
    if (!src) return nullptr;
    
    AvFramePtr dst = createAvFrame();
    if (!dst) return nullptr;
    
    if (av_frame_ref(dst.get(), src) < 0) {
        return nullptr;
    }
    
    return dst;
}

// 创建指定格式的 AVFrame
inline AvFramePtr createAvFrame(int width, int height, AVPixelFormat format) {
    AvFramePtr frame = createAvFrame();
    if (!frame) return nullptr;
    
    frame->width = width;
    frame->height = height;
    frame->format = format;
    
    // 确保像素格式正确设置
    if (av_frame_get_buffer(frame.get(), 32) < 0) {
        return nullptr;
    }
    
    return frame;
}

// 创建音频 AVFrame (简化版本)
inline AvFramePtr createAudioFrame(int nb_samples, AVSampleFormat format, int channels, int sample_rate) {
    AvFramePtr frame = createAvFrame();
    if (!frame) return nullptr;
    
    frame->nb_samples = nb_samples;
    frame->format = format;
    frame->sample_rate = sample_rate;
    
    if (av_frame_get_buffer(frame.get(), 0) < 0) {
        return nullptr;
    }
    
    return frame;
}

} // namespace FFmpegUtils

#endif // AV_FRAME_WRAPPER_H
