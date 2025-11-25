#ifndef AV_CODEC_CONTEXT_WRAPPER_H
#define AV_CODEC_CONTEXT_WRAPPER_H

#include <memory>
#include "FFmpegHeaders.h"

namespace FFmpegUtils {

// AVCodecContext custom deleter
struct AvCodecContextDeleter {
    void operator()(AVCodecContext* context) const {
        if (context) {
            avcodec_free_context(&context);
        }
    }
};

// AVCodecContext smart pointer
using AvCodecContextPtr = std::unique_ptr<AVCodecContext, AvCodecContextDeleter>;

} // namespace FFmpegUtils

#endif // AV_CODEC_CONTEXT_WRAPPER_H
