#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include <QObject>
#include <QAudioSink>
#include <QMediaDevices>
#include <QMutex>
#include <QQueue>
#include <vector>
#include "ffmpeg_resource_manager.h"

class AudioDecoder : public QObject
{
    Q_OBJECT

public:
    explicit AudioDecoder(QObject *parent = nullptr);
    ~AudioDecoder();

    // 初始化音频解码器
    bool init(AVFormatContext *formatCtx, int audioStreamIndex);
    
    // 解码音频包
    void decodePacket(AVPacket *packet);
    
    // 清理资源
    void cleanup();
    
    // 设置音量
    void setVolume(float volume);
    
    // 暂停/恢复音频输出
    void pause();
    void resume();
    
    // 刷新解码器缓冲区
    void flushBuffers();

signals:
    void audioDecoded();

private:
    // FFmpeg 音频相关变量 - 使用智能指针管理
    FFmpeg::TrackedAVCodecContext m_codecCtx;
    FFmpeg::TrackedAVFrame m_frame;
    FFmpeg::TrackedSwrContext m_swrCtx;
    int m_streamIndex = -1;

    // Qt 音频输出
    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioDevice = nullptr;
    float m_volume = 1.0f;
    
    // 音频缓冲区池
    struct AudioBuffer {
        uint8_t* data = nullptr;
        int size = 0;
        bool inUse = false;
    };
    std::vector<AudioBuffer> m_bufferPool;
    static constexpr int BUFFER_POOL_SIZE = 8;
    static constexpr int MAX_BUFFER_SIZE = 192000; // 足够存储1秒的48kHz立体声16位音频
    
    // 同步控制
    QMutex m_mutex;
    bool m_isPaused = false;
};

#endif // AUDIODECODER_H
