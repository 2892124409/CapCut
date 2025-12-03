#ifndef MEDIACONTROLLER_H
#define MEDIACONTROLLER_H

#include "imediaplayer.h"
#include <QObject>
#include <QString>
#include <QQuickFramebufferObject>
#include <QReadWriteLock>
#include <QByteArray>
#include <atomic>

class MediaRenderer;

/**
 * @brief 媒体控制器
 * 
 * 统一管理视频、音频、图片播放器，
 * 提供统一的控制接口给QML使用
 */
class MediaController : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT
    friend class MediaRenderer;

    // === 暴露给 QML 的属性 ===
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingStateChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY pausedStateChanged)
    Q_PROPERTY(bool stopped READ isStopped NOTIFY stoppedStateChanged)

public:
    explicit MediaController(QQuickItem *parent = nullptr);
    ~MediaController() override;

    // QQuickFramebufferObject 渲染入口
    Renderer *createRenderer() const override;

    // === 供 QML 调用的统一接口 ===
    Q_INVOKABLE bool loadMedia(const QString &filePath);
    // 从内存数据加载媒体（通常由 C++ 调用；formatHint 可用于图片格式提示）
    Q_INVOKABLE bool loadVideoFromMemory(const QByteArray &data, const QString &formatHint = QString());
    Q_INVOKABLE bool loadAudioFromMemory(const QByteArray &data, const QString &formatHint = QString());
    Q_INVOKABLE bool loadImageFromMemory(const QByteArray &data, const QString &formatHint = QString());
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 position);
    Q_INVOKABLE void setVolume(float volume);

    // 属性读取器
    qint64 duration() const;
    qint64 position() const;
    bool isPlaying() const;
    bool isPaused() const;
    bool isStopped() const;

signals:
    void durationChanged();
    void positionChanged();
    void playingStateChanged();
    void pausedStateChanged();
    void stoppedStateChanged();
    void errorOccurred(const QString &error);

private slots:
    void onFrameChanged(const QImage &frame);
    void onDurationChanged(qint64 duration);
    void onPositionChanged(qint64 position);
    void onPlayingStateChanged(bool playing);
    void onPausedStateChanged(bool paused);
    void onStoppedStateChanged(bool stopped);
    void onMediaEnded();
    void onErrorOccurred(const QString &error);

private:
    // 创建特定类型的媒体播放器
    IMediaPlayer* createMediaPlayer(const QString &filePath);
    
    // 清理当前播放器
    void cleanupCurrentPlayer();
    
    // 渲染线程安全地获取帧副本
    bool takeFrame(QImage &copy);

    // 当前媒体播放器
    IMediaPlayer* m_currentPlayer = nullptr;
    
    // 渲染相关
    QImage m_currentFrame;
    QReadWriteLock m_frameLock;
    std::atomic<bool> m_frameDirty{false};
    
    // 状态缓存
    std::atomic<qint64> m_cachedDuration{0};
    std::atomic<qint64> m_cachedPosition{0};
    std::atomic<bool> m_cachedPlaying{false};
    std::atomic<bool> m_cachedPaused{false};
    std::atomic<bool> m_cachedStopped{true};
};

#endif // MEDIACONTROLLER_H
