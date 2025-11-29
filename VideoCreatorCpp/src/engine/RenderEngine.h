#ifndef RENDER_ENGINE_H
#define RENDER_ENGINE_H

#include <string>
#include <memory>
#include "model/ProjectConfig.h"
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"
#include "ffmpeg_utils/AvFormatContextWrapper.h"
#include "ffmpeg_utils/AvCodecContextWrapper.h"

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
        double m_totalProjectFrames;
        int m_lastReportedProgress;

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

        // 为转场生成音频淡入淡出 / 交叉混音
        bool renderAudioTransition(const SceneConfig &fromScene, const SceneConfig &toScene, double duration_seconds);

        // 生成测试帧 (用于演示)
        FFmpegUtils::AvFramePtr generateTestFrame(int frameIndex, int width, int height);

        // 从FIFO缓冲区读取并发送固定大小的音频帧
        bool sendBufferedAudioFrames();

        // 冲洗音频缓冲区
        bool flushAudio();

        // 更新并报告进度
        void updateAndReportProgress();

        // flush 编码器剩余包
        bool flushEncoder(AVCodecContext *codecCtx, AVStream *stream);

        // FFmpeg资源
        FFmpegUtils::AvFormatContextPtr m_outputContext;
        FFmpegUtils::AvCodecContextPtr m_videoCodecContext;
        FFmpegUtils::AvCodecContextPtr m_audioCodecContext;
        AVStream *m_videoStream;
        AVStream *m_audioStream;
        AVAudioFifo *m_audioFifo;
        int m_frameCount;
        int64_t m_audioSamplesCount;

        // 是否启用音频转场效果（默认关闭，保留实现以便未来开启）
        bool m_enableAudioTransition;
    };

} // namespace VideoCreator

#endif // RENDER_ENGINE_H
