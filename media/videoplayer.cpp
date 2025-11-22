#include "videoplayer.h"
#include "audiodecoder.h"
#include "videodecoder.h"
#include "decoderworker.h"
#include "imagedecoder.h"
#include <QDebug>
#include <QSGSimpleTextureNode>
#include <QQuickWindow>
#include <QTransform>

VideoPlayer::VideoPlayer(QQuickItem *parent)
    : QQuickItem(parent)
{
    // 【重要】开启 OpenGL 绘制能力
    setFlag(ItemHasContents, true);

    // 创建解码器工作线程
    m_decoderThread = new QThread(this);
    m_decoderWorker = new DecoderWorker();
    m_decoderWorker->moveToThread(m_decoderThread);

    // 连接解码器信号
    connect(m_decoderWorker, &DecoderWorker::videoFrameDecoded,
            this, [this](const QImage &image, qint64 pts)
            {
                // 音画同步优化：检查时间戳是否合理
                if (pts >= 0 && pts <= m_totalDuration.load()) {
                    QWriteLocker locker(&m_imageLock);
                    m_currentImage = image;
                    m_currentPosition.store(pts);
                    emit positionChanged();
                    
                    // 立即更新显示，避免延迟
                    update();
                } });

    // 启动解码器线程
    m_decoderThread->start();

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer); // 使用高精度定时器
    connect(m_timer, &QTimer::timeout, this, &VideoPlayer::onTimerFire);
}

VideoPlayer::~VideoPlayer()
{
    stop();

    // 清理缓存的纹理
    if (m_cachedTexture) {
        delete m_cachedTexture;
        m_cachedTexture = nullptr;
    }

    // 正确清理线程
    if (m_decoderThread)
    {
        m_decoderThread->quit();
        m_decoderThread->wait(); // 等待线程结束
        delete m_decoderThread;
        m_decoderThread = nullptr;
    }

    if (m_decoderWorker)
    {
        delete m_decoderWorker;
        m_decoderWorker = nullptr;
    }
}

void VideoPlayer::stop()
{
    m_timer->stop();

    // 清理多线程解码器
    if (m_decoderWorker)
    {
        m_decoderWorker->cleanup();
    }

    // 清理解码器（向后兼容）
    if (m_audioDecoder)
    {
        m_audioDecoder->cleanup();
        delete m_audioDecoder;
        m_audioDecoder = nullptr;
    }

    if (m_videoDecoder)
    {
        m_videoDecoder->cleanup();
        delete m_videoDecoder;
        m_videoDecoder = nullptr;
    }

    // 清理 FFmpeg 资源
    if (m_formatCtx)
    {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    // 清理缓存的纹理
    if (m_cachedTexture) {
        delete m_cachedTexture;
        m_cachedTexture = nullptr;
    }
    m_cachedTextureSize = QSize();

    m_currentImage = QImage();
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_totalDuration = 0;
    m_currentPosition = 0;
}

void VideoPlayer::play(QString filePath)
{
    stop(); // 先停止
    if (filePath.startsWith("file:///"))
        filePath.remove("file:///");

    m_formatCtx = avformat_alloc_context();
    if (avformat_open_input(&m_formatCtx, filePath.toStdString().c_str(), nullptr, nullptr) != 0)
    {
        qDebug() << "无法打开文件";
        return;
    }

    // 【关键】限制探针，解决起步空白
    m_formatCtx->probesize = 1024 * 1024;
    m_formatCtx->max_analyze_duration = 100000; // 0.1秒
    avformat_find_stream_info(m_formatCtx, nullptr);

    // 获取总时长
    m_totalDuration = m_formatCtx->duration * 1000 / AV_TIME_BASE;
    emit durationChanged();

    // 查找流
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++)
    {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            m_videoStreamIndex = i;
        }
        else if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            m_audioStreamIndex = i;
        }
    }

    // 初始化多线程解码器
    if (m_decoderWorker)
    {
        if (!m_decoderWorker->init(m_formatCtx, m_videoStreamIndex, m_audioStreamIndex))
        {
            qDebug() << "多线程解码器初始化失败";
        }
    }

    // 初始化视频解码器（向后兼容）
    if (m_videoStreamIndex != -1)
    {
        m_videoDecoder = new VideoDecoder(this);
        if (!m_videoDecoder->init(m_formatCtx, m_videoStreamIndex))
        {
            qDebug() << "视频解码器初始化失败";
            delete m_videoDecoder;
            m_videoDecoder = nullptr;
        }
    }

    // 初始化音频解码器（向后兼容）
    if (m_audioStreamIndex != -1)
    {
        m_audioDecoder = new AudioDecoder(this);
        if (!m_audioDecoder->init(m_formatCtx, m_audioStreamIndex))
        {
            qDebug() << "音频解码器初始化失败";
            delete m_audioDecoder;
            m_audioDecoder = nullptr;
        }
    }

    // 启动播放
    if (m_videoStreamIndex != -1)
    {
        m_isFirstFrame = true;
        m_isPaused = false;
        emit pausedChanged();

        m_clockOffset = 0;
        m_masterClock.start();
        m_timer->start(0);

        // 【关键】立即强制解码第一帧，消除启动黑屏
        onTimerFire();
    }
}

void VideoPlayer::pause()
{
    if (m_timer->isActive())
    {
        m_timer->stop();
        bool expected = false;
        if (m_isPaused.compare_exchange_strong(expected, true))
        {
            if (m_audioDecoder)
                m_audioDecoder->pause();
            emit pausedChanged();
        }
    }
}

void VideoPlayer::resume()
{
    bool expected = true;
    if (m_isPaused.compare_exchange_strong(expected, false))
    {
        if (m_audioDecoder)
            m_audioDecoder->resume();

        // 修正时钟：以当前 Seek 的位置为起点
        m_masterClock.restart();
        m_clockOffset.store(m_currentPosition.load());

        m_timer->start(0);
        emit pausedChanged();
    }
}

void VideoPlayer::seek(qint64 ms)
{
    if (!m_formatCtx)
        return;

    // 1. 执行 Seek (Backward)
    int64_t targetTs = ms * AV_TIME_BASE / 1000;
    int ret = av_seek_frame(m_formatCtx, -1, targetTs, AVSEEK_FLAG_BACKWARD);

    if (ret >= 0)
    {
        // 刷新解码器缓冲区
        if (m_videoDecoder)
            m_videoDecoder->flushBuffers();
        if (m_audioDecoder)
            m_audioDecoder->flushBuffers();

        // 2. 【精准 Seek 核心】循环丢帧直到追上目标时间
        AVPacket *packet = av_packet_alloc();
        bool seekSuccess = false;
        int maxRead = 200; // 防止死循环

        while (av_read_frame(m_formatCtx, packet) >= 0 && maxRead-- > 0)
        {
            if (packet->stream_index == m_videoStreamIndex && m_videoDecoder)
            {
                if (m_videoDecoder->decodePacket(packet))
                {
                    // 获取解码后的图像和时间戳
                    QImage decodedImage = m_videoDecoder->getCurrentImage();
                    qint64 decodedPts = m_videoDecoder->getCurrentPts();

                    if (decodedPts >= ms)
                    {
                        {
                            QWriteLocker locker(&m_imageLock);
                            m_currentImage = decodedImage;
                            m_currentPosition.store(decodedPts);
                        }
                        update();
                        seekSuccess = true;
                        break;
                    }
                }
            }
            av_packet_unref(packet);
            if (seekSuccess)
                break;
        }
        av_packet_free(&packet);

        // 3. 更新状态 - 修复音画同步问题
        m_clockOffset.store(ms);
        m_currentPosition.store(ms);
        m_audioPosition.store(ms); // 重置音频位置，避免累积误差
        emit positionChanged();
        m_masterClock.restart();
        m_isFirstFrame.store(false);

        // 4. 如果是暂停状态，不要重启定时器！
        if (!m_isPaused.load())
        {
            m_timer->start(0);
        }
    }
}

void VideoPlayer::onTimerFire()
{
    AVPacket *packet = av_packet_alloc();
    int loopCount = 0;
    bool frameProcessed = false;

    while (av_read_frame(m_formatCtx, packet) >= 0)
    {

        // === 处理音频 ===
        if (packet->stream_index == m_audioStreamIndex && m_audioDecoder)
        {
            m_audioDecoder->decodePacket(packet);
            av_packet_unref(packet);
            continue; // 音频处理完继续读包
        }

        // === 处理视频 ===
        if (packet->stream_index == m_videoStreamIndex && m_videoDecoder)
        {
            if (m_videoDecoder->decodePacket(packet))
            {
                // 获取解码后的图像和时间戳
                QImage decodedImage = m_videoDecoder->getCurrentImage();
                qint64 decodedPts = m_videoDecoder->getCurrentPts();

                {
                    QWriteLocker locker(&m_imageLock);
                    m_currentImage = decodedImage;
                    m_currentPosition.store(decodedPts);
                }

                emit positionChanged();

                // 计算延迟 - 使用简单的主时钟同步
                qint64 elapsed = m_masterClock.elapsed() + m_clockOffset.load();
                double diff = decodedPts - elapsed;

                // 首帧无视时间差
                if (m_isFirstFrame.load())
                {
                    m_isFirstFrame = false;
                    diff = 0;
                    m_masterClock.restart();
                    m_clockOffset = 0;
                }

                // 更新显示
                update();

                av_packet_unref(packet);
                frameProcessed = true;

                // 【关键】暂停状态下，渲染完一帧立刻停止，别重启定时器
                if (m_isPaused.load())
                    break;

                // 简单的时间同步，避免复杂逻辑导致卡顿
                if (diff > 0)
                    m_timer->start(qMin(diff, 33.0)); // 最多延迟33ms（约30fps）
                else
                    m_timer->start(0);

                break; // 退出 while，等待下次 Timer
            }
        }
        av_packet_unref(packet);
        // 防止单次循环过久卡 UI
        if (++loopCount > 100)
        {
            if (!m_isPaused.load())
                m_timer->start(0);
            break;
        }
    }

    // 【关键】检测视频播放完成
    if (!frameProcessed && !m_isPaused.load())
    {
        // 视频播放完成，自动暂停并更新状态
        m_timer->stop();
        m_isPaused = true;
        emit pausedChanged();

        // 确保位置显示为视频结束位置
        if (m_currentPosition.load() < m_totalDuration.load())
        {
            m_currentPosition.store(m_totalDuration.load());
            emit positionChanged();
        }
    }

    av_packet_free(&packet);
}

QSGNode *VideoPlayer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QImage img;
    QSize imgSize;
    {
        QReadLocker locker(&m_imageLock);
        if (m_currentImage.isNull())
        {
            if (oldNode)
                delete oldNode;
            return nullptr;
        }
        img = m_currentImage;
        imgSize = img.size();
    }

    QSGSimpleTextureNode *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    
    // 纹理复用优化：只有当图像尺寸变化时才创建新纹理
    bool needNewTexture = false;
    if (!node || !m_cachedTexture || m_cachedTextureSize != imgSize) {
        needNewTexture = true;
        
        // 清理旧的纹理和节点
        if (m_cachedTexture) {
            delete m_cachedTexture;
            m_cachedTexture = nullptr;
        }
        if (node) {
            delete node;
            node = nullptr;
        }
    }

    if (needNewTexture) {
        // 创建新节点和纹理
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(false); // 我们自己管理纹理生命周期
        
        m_cachedTexture = window()->createTextureFromImage(img);
        if (m_cachedTexture) {
            // 【关键】开启高质量过滤，提升高清视频画质
            m_cachedTexture->setFiltering(QSGTexture::Linear);
            m_cachedTexture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            m_cachedTexture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            m_cachedTextureSize = imgSize;
            node->setTexture(m_cachedTexture);
        } else {
            delete node;
            return nullptr;
        }
    } else {
        // 复用现有纹理，创建新纹理对象但复用OpenGL资源
        QSGTexture *newTexture = window()->createTextureFromImage(img);
        if (newTexture) {
            // 设置相同的过滤和环绕模式
            newTexture->setFiltering(QSGTexture::Linear);
            newTexture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
            newTexture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            
            // 替换纹理
            delete m_cachedTexture;
            m_cachedTexture = newTexture;
            node->setTexture(m_cachedTexture);
        }
    }

    if (node && m_cachedTexture) {
        // 计算 Letterbox 比例
        QRectF rect = boundingRect();
        if (imgSize.isEmpty())
        {
            node->setRect(rect);
        }
        else
        {
            // 高质量缩放计算，保持宽高比
            qreal scale = qMin(rect.width() / imgSize.width(), rect.height() / imgSize.height());
            qreal w = imgSize.width() * scale;
            qreal h = imgSize.height() * scale;
            
            // 确保尺寸为整数像素，避免亚像素渲染模糊
            w = qRound(w);
            h = qRound(h);
            
            qreal x = (rect.width() - w) / 2;
            qreal y = (rect.height() - h) / 2;
            
            // 确保坐标为整数像素
            x = qRound(x);
            y = qRound(y);
            
            node->setRect(x, y, w, h);
        }
    }
    else
    {
        if (node) delete node;
        return nullptr;
    }
    return node;
}

void VideoPlayer::setVolume(float volume)
{
    if (m_audioDecoder)
    {
        m_audioDecoder->setVolume(volume);
    }
}

// ==================== 图片查看功能实现 ====================

void VideoPlayer::loadImage(QString filePath)
{
    stop(); // 先停止视频播放
    
    if (filePath.startsWith("file:///"))
        filePath.remove("file:///");

    // 创建图片解码器
    if (!m_imageDecoder) {
        m_imageDecoder = new ImageDecoder(this);
    }

    // 加载图片
    if (m_imageDecoder->loadImage(filePath)) {
        QWriteLocker locker(&m_imageLock);
        m_currentImage = m_imageDecoder->getCurrentImage();
        m_currentMediaType = "image";
        
        // 重置图片变换参数
        m_zoomLevel = 1.0;
        m_rotationAngle = 0.0;
        updateTransformMatrix();
        
        emit currentMediaTypeChanged();
        emit zoomLevelChanged();
        emit rotationAngleChanged();
        
        // 更新显示
        update();
        
        qDebug() << "图片加载成功:" << filePath;
    } else {
        qDebug() << "图片加载失败:" << filePath;
    }
}

void VideoPlayer::zoomIn()
{
    if (m_currentMediaType == "image") {
        m_zoomLevel = qMin(m_zoomLevel * 1.2, 10.0);
        updateTransformMatrix();
        emit zoomLevelChanged();
        update();
    }
}

void VideoPlayer::zoomOut()
{
    if (m_currentMediaType == "image") {
        m_zoomLevel = qMax(m_zoomLevel / 1.2, 0.1);
        updateTransformMatrix();
        emit zoomLevelChanged();
        update();
    }
}

void VideoPlayer::resetZoom()
{
    if (m_currentMediaType == "image") {
        m_zoomLevel = 1.0;
        updateTransformMatrix();
        emit zoomLevelChanged();
        update();
    }
}

void VideoPlayer::rotateLeft()
{
    if (m_currentMediaType == "image") {
        m_rotationAngle -= 90.0;
        if (m_rotationAngle < 0) {
            m_rotationAngle += 360.0;
        }
        updateTransformMatrix();
        emit rotationAngleChanged();
        update();
    }
}

void VideoPlayer::rotateRight()
{
    if (m_currentMediaType == "image") {
        m_rotationAngle += 90.0;
        if (m_rotationAngle >= 360.0) {
            m_rotationAngle -= 360.0;
        }
        updateTransformMatrix();
        emit rotationAngleChanged();
        update();
    }
}

void VideoPlayer::updateTransformMatrix()
{
    m_transformMatrix.setToIdentity();
    
    if (m_currentMediaType == "image") {
        // 应用缩放
        m_transformMatrix.scale(m_zoomLevel, m_zoomLevel);
        
        // 应用旋转
        m_transformMatrix.rotate(m_rotationAngle, 0, 0, 1);
    }
}

void VideoPlayer::renderImage(QSGSimpleTextureNode* node, const QRectF& rect)
{
    if (!node || !m_cachedTexture) {
        return;
    }

    // 计算图片原始尺寸
    QSize imgSize = m_currentImage.size();
    if (imgSize.isEmpty()) {
        node->setRect(rect);
        return;
    }

    // 计算保持宽高比的缩放
    qreal scale = qMin(rect.width() / imgSize.width(), rect.height() / imgSize.height());
    qreal w = imgSize.width() * scale;
    qreal h = imgSize.height() * scale;
    
    // 应用变换矩阵
    QMatrix4x4 transform = m_transformMatrix;
    transform.scale(scale, scale);
    
    // 计算变换后的尺寸
    QVector3D transformedSize = transform * QVector3D(imgSize.width(), imgSize.height(), 0);
    w = qAbs(transformedSize.x());
    h = qAbs(transformedSize.y());
    
    // 居中显示
    qreal x = (rect.width() - w) / 2;
    qreal y = (rect.height() - h) / 2;
    
    // 确保坐标为整数像素
    x = qRound(x);
    y = qRound(y);
    w = qRound(w);
    h = qRound(h);
    
    node->setRect(x, y, w, h);
}
