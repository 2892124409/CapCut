#ifndef EFFECT_PROCESSOR_H
#define EFFECT_PROCESSOR_H

#include <string>
#include <memory>
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"
#include "model/ProjectConfig.h"

namespace VideoCreator
{

    class EffectProcessor
    {
    public:
        EffectProcessor();
        ~EffectProcessor();

        // 初始化特效处理器
        bool initialize(int width, int height, AVPixelFormat format);

        // 应用Ken Burns特效
        FFmpegUtils::AvFramePtr applyKenBurns(const AVFrame *inputFrame,
                                              const KenBurnsEffect &effect,
                                              double progress);

        // 应用音量混合特效
        std::vector<uint8_t> applyVolumeMix(const std::vector<uint8_t> &audioData,
                                            const VolumeMixEffect &effect,
                                            double progress,
                                            int sampleRate, int channels);

        // 关闭处理器
        void close();

        // 获取错误信息
        std::string getErrorString() const { return m_errorString; }

    private:
        // FFmpeg滤镜相关
        AVFilterGraph *m_filterGraph;
        AVFilterContext *m_buffersrcContext;
        AVFilterContext *m_buffersinkContext;

        int m_width;
        int m_height;
        AVPixelFormat m_pixelFormat;

        std::string m_errorString;

        // 创建Ken Burns滤镜字符串
        std::string createKenBurnsFilterString(const KenBurnsEffect &effect, double progress);

        // 初始化滤镜图
        bool initFilterGraph(const std::string &filterDescription);

        // 清理资源
        void cleanup();
    };

} // namespace VideoCreator

#endif // EFFECT_PROCESSOR_H