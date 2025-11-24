#ifndef RENDER_ENGINE_H
#define RENDER_ENGINE_H

#include <string>
#include <memory>
#include "model/ProjectConfig.h"
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"

namespace VideoCreator
{

    class RenderEngine
    {
    public:
        RenderEngine();
        ~RenderEngine();

        // 初始化渲染引擎
        bool initialize(const ProjectConfig &config);

        // 渲染视频
        bool render();

        // 获取进度 (0-100)
        int progress() const { return m_progress; }

        // 获取错误信息
        std::string errorString() const { return m_errorString; }

    private:
        ProjectConfig m_config;
        int m_progress;
        std::string m_errorString;

        // 创建输出上下文
        bool createOutputContext();

        // 创建视频流
        bool createVideoStream();

        // 创建音频流
        bool createAudioStream();

        // 渲染单个场景
        bool renderScene(const SceneConfig &scene);

        // 渲染转场
        bool renderTransition(const SceneConfig &transitionScene, const SceneConfig &fromScene, const SceneConfig &toScene);

        // 生成测试帧 (用于演示)
        FFmpegUtils::AvFramePtr generateTestFrame(int frameIndex, int width, int height);

        // 清理资源
        void cleanup();

        // flush 编码器剩余包
        bool flushEncoder(AVCodecContext *codecCtx, AVStream *stream);

        // FFmpeg资源
        AVFormatContext *m_outputContext;
        AVCodecContext *m_videoCodecContext;
        AVCodecContext *m_audioCodecContext;
        AVStream *m_videoStream;
        AVStream *m_audioStream;
        int m_frameCount;
        int64_t m_audioSamplesCount;
    };

} // namespace VideoCreator

#endif // RENDER_ENGINE_H
