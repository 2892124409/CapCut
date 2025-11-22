#ifndef DECODERWORKER_H
#define DECODERWORKER_H

#include <QObject>
#include <QMutex>
#include <QQueue>
#include "audiodecoder.h"
#include "videodecoder.h"

// 前向声明
class QImage;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// 数据包结构，用于线程间传递
struct PacketData {
    AVPacket packet;
    int streamIndex;
    qint64 timestamp;
};

class DecoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit DecoderWorker(QObject *parent = nullptr);
    ~DecoderWorker();

    // 初始化解码器
    bool init(AVFormatContext *formatCtx, int videoStreamIndex, int audioStreamIndex);
    
    // 清理资源
    void cleanup();

public slots:
    // 处理数据包
    void processPacket(const PacketData &packetData);
    
    // 刷新解码器
    void flushBuffers();
    
    // 设置音量
    void setVolume(float volume);
    
    // 暂停/恢复音频
    void pauseAudio();
    void resumeAudio();

signals:
    // 解码完成信号
    void videoFrameDecoded(const QImage &image, qint64 pts);
    void audioDataReady(const QByteArray &data, qint64 pts);
    
    // 错误信号
    void errorOccurred(const QString &error);

private:
    // 解码器
    VideoDecoder *m_videoDecoder = nullptr;
    AudioDecoder *m_audioDecoder = nullptr;
    
    // 流索引
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
    
    // 同步控制
    QMutex m_mutex;
    
    // 状态
    bool m_isInitialized = false;
};

#endif // DECODERWORKER_H
