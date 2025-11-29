#include "mediacontroller.h"
#include "videoplayer_impl.h"
#include "audioplayer.h"
#include "imageviewer.h"
#include <QDebug>
#include <QFileInfo>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <algorithm>

// 基础的顶点/片元着色器，绘制一张满屏矩形
static constexpr const char *VERT_SHADER = R"(
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
)";

static constexpr const char *FRAG_SHADER = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texCoord;
uniform sampler2D u_texture;
void main() {
    gl_FragColor = texture2D(u_texture, v_texCoord);
}
)";

class MediaRenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions {
public:
    explicit MediaRenderer(MediaController *controller)
        : m_controller(controller)
    {}

    ~MediaRenderer() override {
        cleanupGL();
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        fmt.setTextureTarget(GL_TEXTURE_2D);
        return new QOpenGLFramebufferObject(size, fmt);
    }

    void synchronize(QQuickFramebufferObject *item) override {
        m_viewport = item->size().toSize();
        if (m_controller) {
            QImage frame;
            if (m_controller->takeFrame(frame)) {
                m_pendingFrame = frame;
                m_frameReady = true;
            }
        }
    }

    void render() override {
        if (!m_initialized) {
            initGL();
            m_initialized = true;
        }

        glViewport(0, 0, m_viewport.width(), m_viewport.height());
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (!updateTexture()) {
            return;
        }

        updateGeometry();

        m_program.bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        m_program.setUniformValue(m_texUniform, 0);

        m_vao.bind();
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
        m_vao.release();

        glBindTexture(GL_TEXTURE_2D, 0);
        m_program.release();
    }

private:
    void initGL() {
        initializeOpenGLFunctions();

        m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, VERT_SHADER);
        m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, FRAG_SHADER);
        m_program.link();

        m_posAttr = m_program.attributeLocation("a_position");
        m_texAttr = m_program.attributeLocation("a_texCoord");
        m_texUniform = m_program.uniformLocation("u_texture");

        static const GLushort INDICES[] = {0, 1, 2, 0, 2, 3};

        m_vao.create();
        m_vao.bind();

        m_vbo.create();
        m_vbo.bind();
        m_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        m_vbo.allocate(sizeof(GLfloat) * 16); // 4 vertices * (pos + uv)

        m_ebo.create();
        m_ebo.bind();
        m_ebo.allocate(INDICES, sizeof(INDICES));

        glEnableVertexAttribArray(m_posAttr);
        glVertexAttribPointer(m_posAttr, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), reinterpret_cast<void *>(0));

        glEnableVertexAttribArray(m_texAttr);
        glVertexAttribPointer(m_texAttr, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), reinterpret_cast<void *>(2 * sizeof(GLfloat)));

        m_vao.release();
        m_vbo.release();
        m_ebo.release();
    }

    bool updateTexture() {
        if (!m_frameReady && m_textureId == 0) {
            return false;
        }

        if (m_frameReady) {
            if (m_pendingFrame.isNull()) {
                if (m_textureId != 0) {
                    glDeleteTextures(1, &m_textureId);
                    m_textureId = 0;
                    m_texSize = QSize();
                }
                m_frameReady = false;
                return false;
            }

            QImage img = m_pendingFrame;
            if (img.format() != QImage::Format_RGBA8888) {
                img = img.convertToFormat(QImage::Format_RGBA8888);
            }

            if (m_textureId == 0) {
                glGenTextures(1, &m_textureId);
            }
            glBindTexture(GL_TEXTURE_2D, m_textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            if (m_texSize != img.size()) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
                m_texSize = img.size();
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width(), img.height(), GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
            }

            glBindTexture(GL_TEXTURE_2D, 0);
            m_frameReady = false;
        }

        return m_textureId != 0;
    }

    void updateGeometry() {
        if (!m_vbo.isCreated() || m_texSize.isEmpty() || m_viewport.isEmpty())
            return;

        const float viewW = static_cast<float>(m_viewport.width());
        const float viewH = static_cast<float>(m_viewport.height());
        const float texW = static_cast<float>(m_texSize.width());
        const float texH = static_cast<float>(m_texSize.height());

        const float scale = std::min(viewW / texW, viewH / texH);
        const float drawW = (texW * scale) / viewW;
        const float drawH = (texH * scale) / viewH;

        const GLfloat vertices[] = {
            -drawW, -drawH, 0.0f, 0.0f,
             drawW, -drawH, 1.0f, 0.0f,
             drawW,  drawH, 1.0f, 1.0f,
            -drawW,  drawH, 0.0f, 1.0f
        };

        m_vbo.bind();
        m_vbo.write(0, vertices, sizeof(vertices));
        m_vbo.release();
    }

    void cleanupGL() {
        if (m_textureId != 0) {
            glDeleteTextures(1, &m_textureId);
            m_textureId = 0;
        }
        if (m_vao.isCreated()) m_vao.destroy();
        if (m_vbo.isCreated()) m_vbo.destroy();
        if (m_ebo.isCreated()) m_ebo.destroy();
        m_program.removeAllShaders();
    }

private:
    MediaController *m_controller = nullptr;
    QOpenGLShaderProgram m_program;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer m_ebo{QOpenGLBuffer::IndexBuffer};

    QSize m_viewport;
    QSize m_texSize;
    GLuint m_textureId = 0;

    bool m_initialized = false;
    bool m_frameReady = false;
    QImage m_pendingFrame;

    int m_posAttr = -1;
    int m_texAttr = -1;
    int m_texUniform = -1;
};
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>

MediaController::MediaController(QQuickItem *parent)
    : QQuickFramebufferObject(parent)
{
    setFlag(ItemHasContents, true);
}

MediaController::~MediaController()
{
    cleanupCurrentPlayer();
}

QQuickFramebufferObject::Renderer *MediaController::createRenderer() const
{
    return new MediaRenderer(const_cast<MediaController *>(this));
}

bool MediaController::takeFrame(QImage &copy)
{
    QReadLocker locker(&m_frameLock);
    if (!m_frameDirty.load())
        return false;

    copy = m_currentFrame;
    m_frameDirty.store(false);
    return true;
}

bool MediaController::loadMedia(const QString &filePath)
{
    cleanupCurrentPlayer();
    
    m_currentPlayer = createMediaPlayer(filePath);
    if (!m_currentPlayer) {
        emit errorOccurred("无法创建媒体播放器");
        return false;
    }
    
    // 连接信号
    connect(m_currentPlayer, &IMediaPlayer::frameChanged, this, &MediaController::onFrameChanged);
    connect(m_currentPlayer, &IMediaPlayer::durationChanged, this, &MediaController::onDurationChanged);
    connect(m_currentPlayer, &IMediaPlayer::positionChanged, this, &MediaController::onPositionChanged);
    connect(m_currentPlayer, &IMediaPlayer::playingStateChanged, this, &MediaController::onPlayingStateChanged);
    connect(m_currentPlayer, &IMediaPlayer::pausedStateChanged, this, &MediaController::onPausedStateChanged);
    connect(m_currentPlayer, &IMediaPlayer::stoppedStateChanged, this, &MediaController::onStoppedStateChanged);
    connect(m_currentPlayer, &IMediaPlayer::mediaEnded, this, &MediaController::onMediaEnded);
    connect(m_currentPlayer, &IMediaPlayer::errorOccurred, this, &MediaController::onErrorOccurred);
    
    bool success = m_currentPlayer->load(filePath);
    if (!success) {
        emit errorOccurred("加载媒体文件失败");
        cleanupCurrentPlayer();
    }
    
    return success;
}

void MediaController::play()
{
    if (m_currentPlayer) {
        m_currentPlayer->play();
    }
}

void MediaController::pause()
{
    if (m_currentPlayer) {
        m_currentPlayer->pause();
    }
}

void MediaController::stop()
{
    if (m_currentPlayer) {
        m_currentPlayer->stop();
    }
}

void MediaController::seek(qint64 position)
{
    if (m_currentPlayer) {
        m_currentPlayer->seek(position);
    }
}

void MediaController::setVolume(float volume)
{
    if (m_currentPlayer) {
        m_currentPlayer->setVolume(volume);
    }
}

// 属性读取器实现
qint64 MediaController::duration() const { return m_cachedDuration.load(); }
qint64 MediaController::position() const { return m_cachedPosition.load(); }
bool MediaController::isPlaying() const { return m_cachedPlaying.load(); }
bool MediaController::isPaused() const { return m_cachedPaused.load(); }
bool MediaController::isStopped() const { return m_cachedStopped.load(); }

// 槽函数实现
void MediaController::onFrameChanged(const QImage &frame)
{
    if (sender() != m_currentPlayer)
        return;
    QWriteLocker locker(&m_frameLock);
    m_currentFrame = frame;
    m_frameDirty.store(true);
    update();
}

void MediaController::onDurationChanged(qint64 duration)
{
    if (sender() != m_currentPlayer)
        return;
    m_cachedDuration.store(duration);
    emit durationChanged();
}

void MediaController::onPositionChanged(qint64 position)
{
    if (sender() != m_currentPlayer)
        return;
    m_cachedPosition.store(position);
    emit positionChanged();
}

void MediaController::onPlayingStateChanged(bool playing)
{
    if (sender() != m_currentPlayer)
        return;
    m_cachedPlaying.store(playing);
    emit playingStateChanged();
}

void MediaController::onPausedStateChanged(bool paused)
{
    if (sender() != m_currentPlayer)
        return;
    m_cachedPaused.store(paused);
    emit pausedStateChanged();
}

void MediaController::onStoppedStateChanged(bool stopped)
{
    if (sender() != m_currentPlayer)
        return;
    m_cachedStopped.store(stopped);
    emit stoppedStateChanged();
}

void MediaController::onMediaEnded()
{
    qDebug() << "MediaController: 媒体播放结束";
    // 可以在这里添加播放结束的处理逻辑
}

void MediaController::onErrorOccurred(const QString &error)
{
    qDebug() << "MediaController: 错误发生:" << error;
    emit errorOccurred(error);
}

// 私有方法实现
IMediaPlayer* MediaController::createMediaPlayer(const QString &filePath)
{
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();
    
    if (extension == "mp4" || extension == "avi" || extension == "mkv" || 
        extension == "mov" || extension == "wmv" || extension == "flv" ||
        extension == "webm" || extension == "m4v" || extension == "3gp" ||
        extension == "ts") {
        return new VideoPlayerImpl(this);
    }
    if (extension == "mp3" || extension == "wav" || extension == "flac" ||
        extension == "aac" || extension == "ogg" || extension == "m4a" ||
        extension == "wma" || extension == "opus" || extension == "aiff" ||
        extension == "ape") {
        return new AudioPlayer(this);
    }
    if (extension == "jpg" || extension == "jpeg" || extension == "png" ||
        extension == "bmp" || extension == "gif" || extension == "tiff" ||
        extension == "tif" || extension == "webp" || extension == "ico" ||
        extension == "svg") {
        return new ImageViewer(this);
    }
    
    return nullptr;
}

void MediaController::cleanupCurrentPlayer()
{
    if (m_currentPlayer) {
        QObject::disconnect(m_currentPlayer, nullptr, this, nullptr);
        m_currentPlayer->stop();
        m_currentPlayer->deleteLater();
        m_currentPlayer = nullptr;
    }
    
    // 重置状态
    m_cachedDuration.store(0);
    m_cachedPosition.store(0);
    m_cachedPlaying.store(false);
    m_cachedPaused.store(false);
    m_cachedStopped.store(true);
    
    {
        QWriteLocker locker(&m_frameLock);
        m_currentFrame = QImage();
        m_frameDirty.store(true);
    }
    
    emit durationChanged();
    emit positionChanged();
    emit playingStateChanged();
    emit pausedStateChanged();
    emit stoppedStateChanged();

    update();
}
