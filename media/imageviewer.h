#ifndef IMAGEVIEWER_H
#define IMAGEVIEWER_H

#include "imediaplayer.h"
#include <QImage>

class ImageViewer : public IMediaPlayer {
    Q_OBJECT
public:
    explicit ImageViewer(QObject *parent = nullptr);
    ~ImageViewer() override = default;

    bool load(const QString &filePath) override;
    bool loadFromData(const QByteArray &data, const QString &formatHint = QString()) override;
    void play() override {}
    void pause() override {}
    void stop() override {}
    void seek(qint64) override {}
    void setVolume(float) override {}

    qint64 duration() const override { return 0; }
    qint64 position() const override { return 0; }
    bool isPlaying() const override { return false; }
    bool isPaused() const override { return false; }
    bool isStopped() const override { return true; }

    QImage currentFrame() const override;

private:
    QImage m_image;
};

#endif // IMAGEVIEWER_H
