#ifndef AV_FORMAT_CONTEXT_WRAPPER_H
#define AV_FORMAT_CONTEXT_WRAPPER_H

#include <memory>
#include "FFmpegHeaders.h"

namespace FFmpegUtils {

// AVFormatContext custom deleter
struct AvFormatContextDeleter {
    void operator()(AVFormatContext* context) const {
        if (context) {
            if (context->pb) {
                avio_closep(&context->pb);
            }
            avformat_free_context(context);
        }
    }
};

// AVFormatContext smart pointer for output contexts
using AvFormatContextPtr = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;

} // namespace FFmpegUtils

#endif // AV_FORMAT_CONTEXT_WRAPPER_H
