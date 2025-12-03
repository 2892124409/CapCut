#ifndef MEDIA_API_H
#define MEDIA_API_H

#include <QObject>
#include <QByteArray>
#include <QImage>
#include <QReadWriteLock>
#include <QString>
#include <atomic>
#include <functional>
#include <string>

class IMediaPlayer;

/**
 * @brief 纯后台播放器入口（不依赖 QML/UI）
 *
 * 复用现有 IMediaPlayer 实现，提供简单的 C++ 控制接口和可选回调。
 * 调用前需保证已有 QCoreApplication/QGuiApplication 事件循环。
 */
class MediaAPI : public QObject {
    Q_OBJECT

public:
    using ErrorCallback = std::function<void(const QString &)>;
    using FrameCallback = std::function<void(const QImage &)>;
    using PositionCallback = std::function<void(qint64)>;
    using StateCallback = std::function<void(bool playing, bool paused, bool stopped)>;
    using EndedCallback = std::function<void()>;

    explicit MediaAPI(QObject *parent = nullptr);
    ~MediaAPI() override;

    bool loadFromPath(const QString &filePath, QString *error = nullptr);
    bool loadFromPath(const std::string &filePath, std::string *error = nullptr);
    bool loadVideoFromMemory(const QByteArray &data, const QString &formatHint = QString(), QString *error = nullptr);
    bool loadAudioFromMemory(const QByteArray &data, const QString &formatHint = QString(), QString *error = nullptr);
    bool loadImageFromMemory(const QByteArray &data, const QString &formatHint = QString(), QString *error = nullptr);

    void play();
    void pause();
    void stop();
    void seek(qint64 positionMs);
    void setVolume(float volume);

    qint64 duration() const;
    qint64 position() const;
    bool isPlaying() const;
    bool isPaused() const;
    bool isStopped() const;
    QString lastError() const;
    QImage currentFrame() const;

    void setErrorCallback(ErrorCallback cb);
    void setFrameCallback(FrameCallback cb);
    void setPositionCallback(PositionCallback cb);
    void setStateCallback(StateCallback cb);
    void setEndedCallback(EndedCallback cb);

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
    IMediaPlayer *createMediaPlayer(const QString &filePath);
    void cleanupCurrentPlayer();
    void connectPlayerSignals(IMediaPlayer *player);

    IMediaPlayer *m_player = nullptr;
    mutable QReadWriteLock m_frameLock;
    QImage m_lastFrame;
    std::atomic<qint64> m_cachedDuration{0};
    std::atomic<qint64> m_cachedPosition{0};
    std::atomic<bool> m_cachedPlaying{false};
    std::atomic<bool> m_cachedPaused{false};
    std::atomic<bool> m_cachedStopped{true};
    QString m_lastError;

    ErrorCallback m_errorCallback;
    FrameCallback m_frameCallback;
    PositionCallback m_positionCallback;
    StateCallback m_stateCallback;
    EndedCallback m_endedCallback;
};

#endif // MEDIA_API_H
