#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include <QObject>
#include <QAudioSink>
#include <QMediaDevices>
#include <QMutex>
#include <vector>
#include <QAudioFormat> // 【新增】
#include "ffmpeg_resource_manager.h"

class AudioDecoder : public QObject
{
    Q_OBJECT

public:
    explicit AudioDecoder(QObject *parent = nullptr);
    ~AudioDecoder();

    bool init(AVFormatContext *formatCtx, int audioStreamIndex);
    void decodePacket(AVPacket *packet);
    void cleanup();
    void setVolume(float volume);
    void pause();
    void resume();
    void flushBuffers();

    // 获取音频缓冲区剩余空间
    qint64 bytesFree() const;

signals:
    void audioDecoded();

private:
    // 【新增】辅助函数：重建音频输出
    bool recreateAudioOutput();

    FFmpeg::TrackedAVCodecContext m_codecCtx;
    FFmpeg::TrackedAVFrame m_frame;
    FFmpeg::TrackedSwrContext m_swrCtx;
    int m_streamIndex = -1;

    QAudioSink *m_audioSink = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QAudioFormat m_outputFormat; // 【新增】保存协商好的格式
    float m_volume = 1.0f;

    struct AudioBuffer
    {
        uint8_t *data = nullptr;
        int size = 0;
        bool inUse = false;
    };
    std::vector<AudioBuffer> m_bufferPool;
    static constexpr int BUFFER_POOL_SIZE = 8;
    static constexpr int MAX_BUFFER_SIZE = 192000;

    mutable QMutex m_mutex;
    bool m_isPaused = false;
};

#endif // AUDIODECODER_H