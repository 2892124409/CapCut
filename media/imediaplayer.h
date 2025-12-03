#ifndef IMEDIAPLAYER_H
#define IMEDIAPLAYER_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QByteArray>

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

    // === 媒体控制接口（基于文件路径） ===
    virtual bool load(const QString &filePath) = 0;
    // === 媒体控制接口（基于内存数据） ===
    // 默认返回 false，实际播放器可选择实现
    virtual bool loadFromData(const QByteArray &data, const QString &formatHint = QString()) {
        Q_UNUSED(data);
        Q_UNUSED(formatHint);
        return false;
    }
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 position) = 0;
    virtual void setVolume(float volume) = 0;

    // === 状态查询接口 ===
    virtual qint64 duration() const = 0;
    virtual qint64 position() const = 0;
    virtual bool isPlaying() const = 0;
    virtual bool isPaused() const = 0;
    virtual bool isStopped() const = 0;

    // === 媒体特定功能接口 ===
    virtual QImage currentFrame() const = 0;

signals:
    // 状态变化信号
    void durationChanged(qint64 duration);
    void positionChanged(qint64 position);
    void playingStateChanged(bool playing);
    void pausedStateChanged(bool paused);
    void stoppedStateChanged(bool stopped);
    void frameChanged(const QImage &frame);
    void errorOccurred(const QString &error);
    void mediaEnded();
};

#endif // IMEDIAPLAYER_H
