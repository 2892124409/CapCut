#ifndef AV_PACKET_WRAPPER_H
#define AV_PACKET_WRAPPER_H

#include <memory>
#include "FFmpegHeaders.h"

namespace FFmpegUtils {

// AVPacket 自定义删除器
struct AvPacketDeleter {
    void operator()(AVPacket* packet) const {
        if (packet) {
            av_packet_free(&packet);
        }
    }
};

// AVPacket 智能指针
using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketDeleter>;

// 创建 AVPacket 智能指针
inline AvPacketPtr createAvPacket() {
    AVPacket* packet = av_packet_alloc();
    return AvPacketPtr(packet);
}

// 复制 AVPacket
inline AvPacketPtr copyAvPacket(const AVPacket* src) {
    if (!src) return nullptr;
    
    AVPacket* dst = av_packet_alloc();
    if (!dst) return nullptr;
    
    if (av_packet_ref(dst, src) < 0) {
        av_packet_free(&dst);
        return nullptr;
    }
    
    return AvPacketPtr(dst);
}

} // namespace FFmpegUtils

#endif // AV_PACKET_WRAPPER_H
