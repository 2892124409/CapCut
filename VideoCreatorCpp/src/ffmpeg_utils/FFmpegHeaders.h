#ifndef FFMPEG_HEADERS_H
#define FFMPEG_HEADERS_H

// 统一包含FFmpeg头文件，处理C++兼容性
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

#endif // FFMPEG_HEADERS_H
