#include "imagedecoder.h"
#include <QDebug>
#include <QTransform>

ImageDecoder::ImageDecoder(QObject *parent)
    : QObject(parent)
{
}

ImageDecoder::~ImageDecoder()
{
    cleanup();
}

bool ImageDecoder::loadImage(const QString& filePath)
{
    cleanup(); // 先清理之前的资源

    // 使用FFmpeg打开图片文件
    if (avformat_open_input(&m_formatCtx, filePath.toStdString().c_str(), nullptr, nullptr) < 0) {
        qDebug() << "无法打开图片文件:" << filePath;
        return false;
    }

    // 查找流信息
    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        qDebug() << "无法获取图片流信息";
        cleanup();
        return false;
    }

    // 查找图片流
    m_streamIndex = -1;
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_streamIndex = i;
            break;
        }
    }

    if (m_streamIndex == -1) {
        qDebug() << "未找到图片流";
        cleanup();
        return false;
    }

    // 获取解码器
    AVCodecParameters* codecParams = m_formatCtx->streams[m_streamIndex]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        qDebug() << "不支持的解码器";
        cleanup();
        return false;
    }

    // 创建解码器上下文
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qDebug() << "无法分配解码器上下文";
        cleanup();
        return false;
    }

    // 复制参数到解码器上下文
    if (avcodec_parameters_to_context(m_codecCtx, codecParams) < 0) {
        qDebug() << "无法复制解码器参数";
        cleanup();
        return false;
    }

    // 打开解码器
    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        qDebug() << "无法打开解码器";
        cleanup();
        return false;
    }

    // 读取并解码图片
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool success = false;

    while (av_read_frame(m_formatCtx, packet) >= 0) {
        if (packet->stream_index == m_streamIndex) {
            // 发送包到解码器
            if (avcodec_send_packet(m_codecCtx, packet) == 0) {
                // 接收解码后的帧
                if (avcodec_receive_frame(m_codecCtx, frame) == 0) {
                    // 转换为QImage
                    QImage image = convertAVFrameToQImage(frame);
                    if (!image.isNull()) {
                        QWriteLocker locker(&m_imageLock);
                        m_currentImage = image;
                        success = true;
                    }
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);

    if (success) {
        qDebug() << "图片加载成功:" << filePath << "尺寸:" << m_currentImage.size();
        resetTransform(); // 重置变换参数
    } else {
        qDebug() << "图片解码失败:" << filePath;
        cleanup();
    }

    return success;
}

QImage ImageDecoder::getCurrentImage() const
{
    QReadLocker locker(&m_imageLock);
    return m_currentImage;
}

void ImageDecoder::cleanup()
{
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }

    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    m_streamIndex = -1;

    QWriteLocker locker(&m_imageLock);
    m_currentImage = QImage();
}

void ImageDecoder::setZoomLevel(qreal zoom)
{
    if (zoom > 0.1 && zoom < 10.0) {
        m_zoomLevel = zoom;
        updateTransformMatrix();
    }
}

void ImageDecoder::setRotationAngle(qreal angle)
{
    m_rotationAngle = angle;
    updateTransformMatrix();
}

void ImageDecoder::resetTransform()
{
    m_zoomLevel = 1.0;
    m_rotationAngle = 0.0;
    updateTransformMatrix();
}

void ImageDecoder::updateTransformMatrix()
{
    m_transformMatrix.setToIdentity();
    
    // 应用缩放
    m_transformMatrix.scale(m_zoomLevel, m_zoomLevel);
    
    // 应用旋转
    m_transformMatrix.rotate(m_rotationAngle, 0, 0, 1);
}

QImage ImageDecoder::convertAVFrameToQImage(AVFrame* frame)
{
    if (!frame || !frame->data[0]) {
        return QImage();
    }

    // 创建SWS上下文进行格式转换
    m_swsCtx = sws_getContext(
        frame->width, frame->height,
        (AVPixelFormat)frame->format,
        frame->width, frame->height,
        AV_PIX_FMT_RGB32,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!m_swsCtx) {
        return QImage();
    }

    // 创建目标帧
    AVFrame* rgbFrame = av_frame_alloc();
    rgbFrame->format = AV_PIX_FMT_RGB32;
    rgbFrame->width = frame->width;
    rgbFrame->height = frame->height;

    if (av_frame_get_buffer(rgbFrame, 0) < 0) {
        av_frame_free(&rgbFrame);
        return QImage();
    }

    // 转换格式
    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, frame->height,
              rgbFrame->data, rgbFrame->linesize);

    // 创建QImage
    QImage image(rgbFrame->data[0], rgbFrame->width, rgbFrame->height,
                 rgbFrame->linesize[0], QImage::Format_RGB32);

    // 复制图像数据（因为rgbFrame会被释放）
    QImage result = image.copy();

    av_frame_free(&rgbFrame);
    sws_freeContext(m_swsCtx);
    m_swsCtx = nullptr;

    return result;
}
