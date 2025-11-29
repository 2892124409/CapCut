#ifndef IMEDIAPLAYER_H
#define IMEDIAPLAYER_H

#include <QObject>
#include <QString>
#include <QImage>

/**
 * @brief 媒体播放器统一接口
 * 
 * 定义了视频、音频、图片播放器的统一接口，
 * 便于MediaController统一管理不同类型的媒体
 */
class IMediaPlayer : public QObject {
    Q_OBJECT

public:
    explicit IMediaPlayer(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IMediaPlayer() = default;

    // === 媒体控制接口 ===
    
    /**
     * @brief 加载媒体文件
     * @param filePath 文件路径
     * @return 是否加载成功
     */
    virtual bool load(const QString &filePath) = 0;
    
    /**
     * @brief 播放媒体
     */
    virtual void play() = 0;
    
    /**
     * @brief 暂停播放
     */
    virtual void pause() = 0;
    
    /**
     * @brief 停止播放
     */
    virtual void stop() = 0;
    
    /**
     * @brief 跳转到指定位置
     * @param position 位置（毫秒）
     */
    virtual void seek(qint64 position) = 0;
    
    /**
     * @brief 设置音量
     * @param volume 音量值 [0.0, 1.0]
     */
    virtual void setVolume(float volume) = 0;

    // === 状态查询接口 ===
    
    /**
     * @brief 获取媒体时长
     * @return 时长（毫秒）
     */
    virtual qint64 duration() const = 0;
    
    /**
     * @brief 获取当前播放位置
     * @return 位置（毫秒）
     */
    virtual qint64 position() const = 0;
    
    /**
     * @brief 是否正在播放
     * @return true-播放中，false-暂停或停止
     */
    virtual bool isPlaying() const = 0;
    
    /**
     * @brief 是否已暂停
     * @return true-已暂停
     */
    virtual bool isPaused() const = 0;
    
    /**
     * @brief 是否已停止
     * @return true-已停止
     */
    virtual bool isStopped() const = 0;
    
    /**
     * @brief 获取媒体类型
     * @return "video", "audio", "image"
     */
    virtual QString mediaType() const = 0;

    // === 媒体特定功能接口 ===
    
    /**
     * @brief 获取当前帧图像（视频/图片）
     * @return 当前图像
     */
    virtual QImage currentFrame() const = 0;
    
    /**
     * @brief 是否支持缩放
     * @return true-支持缩放
     */
    virtual bool supportsZoom() const = 0;
    
    /**
     * @brief 是否支持旋转
     * @return true-支持旋转
     */
    virtual bool supportsRotation() const = 0;
    
    /**
     * @brief 获取缩放级别
     * @return 当前缩放级别
     */
    virtual qreal zoomLevel() const = 0;
    
    /**
     * @brief 获取旋转角度
     * @return 当前旋转角度
     */
    virtual qreal rotationAngle() const = 0;
    
    /**
     * @brief 设置缩放级别
     * @param zoom 缩放级别
     */
    virtual void setZoomLevel(qreal zoom) = 0;
    
    /**
     * @brief 设置旋转角度
     * @param angle 旋转角度
     */
    virtual void setRotationAngle(qreal angle) = 0;
    
    /**
     * @brief 重置变换
     */
    virtual void resetTransform() = 0;

signals:
    // 状态变化信号
    void durationChanged(qint64 duration);
    void positionChanged(qint64 position);
    void playingStateChanged(bool playing);
    void pausedStateChanged(bool paused);
    void stoppedStateChanged(bool stopped);
    
    // 媒体特定信号
    void frameChanged(const QImage &frame);
    void zoomLevelChanged(qreal zoom);
    void rotationAngleChanged(qreal angle);
    
    // 错误信号
    void errorOccurred(const QString &error);
    void mediaEnded();
};

#endif // IMEDIAPLAYER_H
