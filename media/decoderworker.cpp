#include "decoderworker.h"
#include <QDebug>
#include <QImage>

DecoderWorker::DecoderWorker(QObject *parent)
    : QObject(parent)
{
}

DecoderWorker::~DecoderWorker()
{
    cleanup();
}

bool DecoderWorker::init(AVFormatContext *formatCtx, int videoStreamIndex, int audioStreamIndex)
{
    QMutexLocker locker(&m_mutex);
    
    if (!formatCtx) {
        emit errorOccurred("Format context is null");
        return false;
    }
    
    m_videoStreamIndex = videoStreamIndex;
    m_audioStreamIndex = audioStreamIndex;
    
    // 初始化视频解码器
    if (m_videoStreamIndex != -1) {
        m_videoDecoder = new VideoDecoder(this);
        if (!m_videoDecoder->init(formatCtx, m_videoStreamIndex)) {
            emit errorOccurred("视频解码器初始化失败");
            delete m_videoDecoder;
            m_videoDecoder = nullptr;
            return false;
        }
        
        // 连接视频解码信号
        connect(m_videoDecoder, &VideoDecoder::frameDecoded, 
                this, &DecoderWorker::videoFrameDecoded);
    }
    
    // 初始化音频解码器
    if (m_audioStreamIndex != -1) {
        m_audioDecoder = new AudioDecoder(this);
        if (!m_audioDecoder->init(formatCtx, m_audioStreamIndex)) {
            emit errorOccurred("音频解码器初始化失败");
            delete m_audioDecoder;
            m_audioDecoder = nullptr;
            // 视频解码器已经初始化成功，不返回false
        }
    }
    
    m_isInitialized = true;
    return true;
}

void DecoderWorker::cleanup()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_videoDecoder) {
        m_videoDecoder->cleanup();
        delete m_videoDecoder;
        m_videoDecoder = nullptr;
    }
    
    if (m_audioDecoder) {
        m_audioDecoder->cleanup();
        delete m_audioDecoder;
        m_audioDecoder = nullptr;
    }
    
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_isInitialized = false;
}

void DecoderWorker::processPacket(const PacketData &packetData)
{
    QMutexLocker locker(&m_mutex);
    
    if (!m_isInitialized) {
        return;
    }
    
    // 复制数据包，避免线程安全问题
    AVPacket packet;
    av_packet_ref(&packet, &packetData.packet);
    
    // 根据流类型分发到对应的解码器
    if (packet.stream_index == m_videoStreamIndex && m_videoDecoder) {
        if (m_videoDecoder->decodePacket(&packet)) {
            // 信号已经在VideoDecoder中发出
        }
    } else if (packet.stream_index == m_audioStreamIndex && m_audioDecoder) {
        m_audioDecoder->decodePacket(&packet);
    }
    
    av_packet_unref(&packet);
}

void DecoderWorker::flushBuffers()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_videoDecoder) {
        m_videoDecoder->flushBuffers();
    }
    
    if (m_audioDecoder) {
        m_audioDecoder->flushBuffers();
    }
}

void DecoderWorker::setVolume(float volume)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_audioDecoder) {
        m_audioDecoder->setVolume(volume);
    }
}

void DecoderWorker::pauseAudio()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_audioDecoder) {
        m_audioDecoder->pause();
    }
}

void DecoderWorker::resumeAudio()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_audioDecoder) {
        m_audioDecoder->resume();
    }
}
