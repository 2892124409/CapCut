#include "videodecoder.h"
#include <QDebug>

VideoDecoder::VideoDecoder(QObject *parent)
    : QObject(parent)
{
}

VideoDecoder::~VideoDecoder()
{
    cleanup();
}

bool VideoDecoder::init(AVFormatContext *formatCtx, int videoStreamIndex)
{
    QMutexLocker locker(&m_mutex);

    if (!formatCtx || videoStreamIndex < 0)
    {
        return false;
    }

    m_streamIndex = videoStreamIndex;
    m_formatCtx = formatCtx; // 保存formatCtx用于时间戳计算
    AVCodecParameters *vPar = formatCtx->streams[videoStreamIndex]->codecpar;

    // 查找解码器
    const AVCodec *vCodec = avcodec_find_decoder(vPar->codec_id);
    if (!vCodec)
    {
        qDebug() << "无法找到视频解码器";
        return false;
    }

    // 分配解码器上下文
    AVCodecContext *codecCtx = avcodec_alloc_context3(vCodec);
    if (!codecCtx)
    {
        qDebug() << "无法分配解码器上下文";
        return false;
    }

    m_codecCtx.reset(codecCtx, "AVCodecContext");
    avcodec_parameters_to_context(m_codecCtx.get(), vPar);

    if (avcodec_open2(m_codecCtx.get(), vCodec, nullptr) < 0)
    {
        qDebug() << "无法打开视频解码器";
        m_codecCtx.reset(nullptr, "AVCodecContext");
        return false;
    }

    // 分配帧
    AVFrame *frame = av_frame_alloc();
    AVFrame *frameRGB = av_frame_alloc();
    if (!frame || !frameRGB)
    {
        qDebug() << "无法分配视频帧";
        return false;
    }

    m_frame.reset(frame, "AVFrame");
    m_frameRGB.reset(frameRGB, "AVFrame");

    // 分配 RGB 缓冲区
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32,
                                            m_codecCtx->width,
                                            m_codecCtx->height, 1);
    m_rgbBuffer.reset((uint8_t *)av_malloc(numBytes * sizeof(uint8_t)));
    if (!m_rgbBuffer)
    {
        qDebug() << "无法分配RGB缓冲区";
        return false;
    }

    av_image_fill_arrays(m_frameRGB->data, m_frameRGB->linesize, m_rgbBuffer.get(),
                         AV_PIX_FMT_RGB32, m_codecCtx->width, m_codecCtx->height, 1);

    // 初始化图像转换器 - 使用高质量缩放算法
    SwsContext *swsCtx = sws_getContext(m_codecCtx->width, m_codecCtx->height,
                                        m_codecCtx->pix_fmt,
                                        m_codecCtx->width, m_codecCtx->height,
                                        AV_PIX_FMT_RGB32,
                                        SWS_LANCZOS, nullptr, nullptr, nullptr);
    if (!swsCtx)
    {
        qDebug() << "无法创建图像转换器";
        m_rgbBuffer.reset();
        return false;
    }

    m_swsCtx.reset(swsCtx, "SwsContext");

    qDebug() << "视频解码器初始化成功，尺寸:" << m_codecCtx->width << "x" << m_codecCtx->height;
    return true;
}

bool VideoDecoder::decodePacket(AVPacket *packet)
{
    QMutexLocker locker(&m_mutex);

    if (!m_codecCtx || !m_frame || !m_frameRGB || !m_formatCtx)
    {
        return false;
    }

    if (avcodec_send_packet(m_codecCtx.get(), packet) == 0)
    {
        if (avcodec_receive_frame(m_codecCtx.get(), m_frame.get()) == 0)
        {
            // 转换到 RGB 格式
            sws_scale(m_swsCtx.get(),
                      (const uint8_t *const *)m_frame->data, m_frame->linesize,
                      0, m_codecCtx->height,
                      m_frameRGB->data, m_frameRGB->linesize);

            // 创建 QImage - 避免不必要的复制
            m_currentImage = QImage(m_frameRGB->data[0], m_codecCtx->width, m_codecCtx->height,
                                    m_frameRGB->linesize[0], QImage::Format_RGB32);

            // 修复时间戳计算 - 关键修复
            // 使用流的时间基而不是解码器的时间基
            AVRational time_base = m_formatCtx->streams[m_streamIndex]->time_base;

            if (m_frame->best_effort_timestamp != AV_NOPTS_VALUE)
            {
                m_currentPts = m_frame->best_effort_timestamp * av_q2d(time_base) * 1000.0;
            }
            else if (m_frame->pts != AV_NOPTS_VALUE)
            {
                m_currentPts = m_frame->pts * av_q2d(time_base) * 1000.0;
            }
            else if (packet->pts != AV_NOPTS_VALUE)
            {
                m_currentPts = packet->pts * av_q2d(time_base) * 1000.0;
            }
            else
            {
                // 如果没有时间戳，使用帧号估算
                static int64_t frameCount = 0;
                m_currentPts = frameCount++ * (1000.0 / 25.0); // 假设25fps
            }

            emit frameDecoded(m_currentImage, m_currentPts);
            return true;
        }
    }
    return false;
}

QImage VideoDecoder::getCurrentImage()
{
    QMutexLocker locker(&m_mutex);
    return m_currentImage;
}

void VideoDecoder::cleanup()
{
    QMutexLocker locker(&m_mutex);

    // 智能指针会自动清理所有资源，包括RGB缓冲区
    m_rgbBuffer.reset();
    m_codecCtx.reset(nullptr, "AVCodecContext");
    m_frame.reset(nullptr, "AVFrame");
    m_frameRGB.reset(nullptr, "AVFrame");
    m_swsCtx.reset(nullptr, "SwsContext");

    m_streamIndex = -1;
    m_currentImage = QImage();
    m_currentPts = 0;
    m_formatCtx = nullptr;

    qDebug() << "视频解码器资源清理完成";
}

void VideoDecoder::flushBuffers()
{
    QMutexLocker locker(&m_mutex);
    if (m_codecCtx)
    {
        avcodec_flush_buffers(m_codecCtx.get());
        qDebug() << "视频解码器缓冲区已刷新";
    }
}

QSize VideoDecoder::getVideoSize() const
{
    if (m_codecCtx)
    {
        return QSize(m_codecCtx->width, m_codecCtx->height);
    }
    return QSize();
}
