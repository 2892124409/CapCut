#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include "ffmpeg_resource_manager.h"

class VideoDecoder : public QObject
{
    Q_OBJECT

public:
    explicit VideoDecoder(QObject *parent = nullptr);
    ~VideoDecoder();

    // 初始化视频解码器
    bool init(AVFormatContext *formatCtx, int videoStreamIndex);

    // 解码视频包
    bool decodePacket(AVPacket *packet);

    // 获取当前解码的图像
    QImage getCurrentImage();

    // 获取当前帧的时间戳
    qint64 getCurrentPts() const { return m_currentPts; }

    // 清理资源
    void cleanup();

    // 刷新解码器缓冲区
    void flushBuffers();

    // 获取视频尺寸
    QSize getVideoSize() const;

signals:
    void frameDecoded(const QImage &image, qint64 pts);

private:
    // FFmpeg 视频相关变量 - 使用智能指针管理
    AVFormatContext *m_formatCtx = nullptr; // 不拥有所有权，由外部管理
    FFmpeg::TrackedAVCodecContext m_codecCtx;
    FFmpeg::TrackedAVFrame m_frame;
    FFmpeg::TrackedAVFrame m_frameRGB;
    FFmpeg::TrackedSwsContext m_swsCtx;
    int m_streamIndex = -1;

    // 当前状态
    QImage m_currentImage;
    qint64 m_currentPts = 0;

    // 同步控制
    QMutex m_mutex;

    // RGB缓冲区 - 使用智能指针自动管理
    std::unique_ptr<uint8_t, decltype(&av_free)> m_rgbBuffer{nullptr, &av_free};
};

#endif // VIDEODECODER_H
