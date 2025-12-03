#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include "imediaplayer.h"
#include "demuxer.h"
#include "audiodecoder.h"
#include <QTimer>
#include <atomic>

class AudioPlayer : public IMediaPlayer {
    Q_OBJECT
public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer() override;

    bool load(const QString &filePath) override;
    bool loadFromData(const QByteArray &data, const QString &formatHint = QString()) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 position) override;
    void setVolume(float volume) override;

    qint64 duration() const override;
    qint64 position() const override;
    bool isPlaying() const override;
    bool isPaused() const override;
    bool isStopped() const override;

    QImage currentFrame() const override { return QImage(); }

private slots:
    void onDemuxerOpened(qint64 duration, int videoStreamIndex, int audioStreamIndex);
    void onDemuxerEndOfFile();
    void onDemuxerFailedToOpen(const QString &error);
    void onTimerFire();

private:
    void cleanup();

    QTimer *m_timer = nullptr;
    Demuxer *m_demuxer = nullptr;
    AudioDecoder *m_audioDecoder = nullptr;
    bool m_usingMemorySource = false;
    QByteArray m_currentMemoryData;

    std::atomic<qint64> m_totalDuration{0};
    std::atomic<qint64> m_currentPosition{0};
    std::atomic<bool> m_isPaused{false};
    std::atomic<bool> m_isStopped{true};
    int m_audioStreamIndex = -1;
};

#endif // AUDIOPLAYER_H
