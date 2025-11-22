#ifndef FFMPEG_RESOURCE_MANAGER_H
#define FFMPEG_RESOURCE_MANAGER_H

#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <string>
#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
}

// FFmpeg资源智能指针包装器
namespace FFmpeg {

// 自定义删除器
struct AVFormatContextDeleter {
    void operator()(AVFormatContext* ctx) const {
        if (ctx) {
            avformat_close_input(&ctx);
            qDebug() << "AVFormatContext 已释放";
        }
    }
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* ctx) const {
        if (ctx) {
            avcodec_free_context(&ctx);
            qDebug() << "AVCodecContext 已释放";
        }
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* frame) const {
        if (frame) {
            av_frame_free(&frame);
            qDebug() << "AVFrame 已释放";
        }
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* ctx) const {
        if (ctx) {
            sws_freeContext(ctx);
            qDebug() << "SwsContext 已释放";
        }
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* ctx) const {
        if (ctx) {
            swr_free(&ctx);
            qDebug() << "SwrContext 已释放";
        }
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* packet) const {
        if (packet) {
            av_packet_free(&packet);
            qDebug() << "AVPacket 已释放";
        }
    }
};

// 智能指针类型定义
using AVFormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using AVFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

// 资源创建函数
inline AVFormatContextPtr createFormatContext() {
    AVFormatContext* ctx = avformat_alloc_context();
    return AVFormatContextPtr(ctx);
}

inline AVCodecContextPtr createCodecContext(const AVCodec* codec) {
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    return AVCodecContextPtr(ctx);
}

inline AVFramePtr createFrame() {
    AVFrame* frame = av_frame_alloc();
    return AVFramePtr(frame);
}

inline AVPacketPtr createPacket() {
    AVPacket* packet = av_packet_alloc();
    return AVPacketPtr(packet);
}

// 内存泄漏检测工具
class MemoryLeakDetector {
public:
    static MemoryLeakDetector& instance() {
        static MemoryLeakDetector detector;
        return detector;
    }

    void registerResource(const std::string& type, void* ptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_resources[type].insert(ptr);
        qDebug() << "注册资源:" << type.c_str() << ptr;
    }

    void unregisterResource(const std::string& type, void* ptr) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& resources = m_resources[type];
        auto it = resources.find(ptr);
        if (it != resources.end()) {
            resources.erase(it);
            qDebug() << "释放资源:" << type.c_str() << ptr;
        }
    }

    void reportLeaks() {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool hasLeaks = false;
        
        for (const auto& [type, resources] : m_resources) {
            if (!resources.empty()) {
                hasLeaks = true;
                qDebug() << "内存泄漏检测 -" << type.c_str() << ":" << resources.size() << "个未释放资源";
                for (void* ptr : resources) {
                    qDebug() << "  - 泄漏地址:" << ptr;
                }
            }
        }
        
        if (!hasLeaks) {
            qDebug() << "内存泄漏检测: 未发现泄漏";
        }
    }

private:
    std::mutex m_mutex;
    std::unordered_map<std::string, std::unordered_set<void*>> m_resources;
};

// 带内存检测的资源包装器
template<typename T, typename Deleter>
class TrackedResource {
public:
    // 默认构造函数
    TrackedResource() : m_ptr(nullptr, Deleter()), m_type("Unknown") {}
    
    // 带参数的构造函数
    TrackedResource(T* ptr, const std::string& type) 
        : m_ptr(ptr, Deleter()), m_type(type) {
        if (ptr) {
            MemoryLeakDetector::instance().registerResource(type, ptr);
        }
    }

    // 移动构造函数
    TrackedResource(TrackedResource&& other) noexcept
        : m_ptr(std::move(other.m_ptr)), m_type(std::move(other.m_type)) {
    }

    // 移动赋值操作符
    TrackedResource& operator=(TrackedResource&& other) noexcept {
        if (this != &other) {
            // 先注销当前资源
            if (m_ptr) {
                MemoryLeakDetector::instance().unregisterResource(m_type, m_ptr.get());
            }
            m_ptr = std::move(other.m_ptr);
            m_type = std::move(other.m_type);
        }
        return *this;
    }

    ~TrackedResource() {
        if (m_ptr) {
            MemoryLeakDetector::instance().unregisterResource(m_type, m_ptr.get());
        }
    }

    T* get() const { return m_ptr.get(); }
    T* operator->() const { return m_ptr.get(); }
    T& operator*() const { return *m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

    // 重置资源
    void reset(T* ptr = nullptr, const std::string& type = "Unknown") {
        // 先注销当前资源
        if (m_ptr) {
            MemoryLeakDetector::instance().unregisterResource(m_type, m_ptr.get());
        }
        m_ptr.reset(ptr);
        m_type = type;
        // 注册新资源
        if (ptr) {
            MemoryLeakDetector::instance().registerResource(type, ptr);
        }
    }

private:
    std::unique_ptr<T, Deleter> m_ptr;
    std::string m_type;
};

// 带跟踪的资源类型定义
using TrackedAVFormatContext = TrackedResource<AVFormatContext, AVFormatContextDeleter>;
using TrackedAVCodecContext = TrackedResource<AVCodecContext, AVCodecContextDeleter>;
using TrackedAVFrame = TrackedResource<AVFrame, AVFrameDeleter>;
using TrackedSwsContext = TrackedResource<SwsContext, SwsContextDeleter>;
using TrackedSwrContext = TrackedResource<SwrContext, SwrContextDeleter>;
using TrackedAVPacket = TrackedResource<AVPacket, AVPacketDeleter>;

} // namespace FFmpeg

#endif // FFMPEG_RESOURCE_MANAGER_H
