#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <string>
#include <memory>
#include <vector>
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"
#include "model/ProjectConfig.h" // 包含场景配置的头文件

namespace VideoCreator
{

    class AudioDecoder
    {
    public:
        AudioDecoder();
        ~AudioDecoder();

        // 打开音频文件
        bool open(const std::string &filePath);

        // 应用音量效果
        bool applyVolumeEffect(const SceneConfig& sceneConfig);
        bool applyVolumeEffect(double baseVolume, const VolumeMixEffect* effect, double trackDurationSeconds);

        // 尝试解码下一帧音频，重采样并应用效果后返回
        // 返回值: >0 表示成功, 0 表示文件结束(EOF), <0 表示错误
        int decodeFrame(FFmpegUtils::AvFramePtr &frame);

        // 跳转到指定时间戳 (秒)
        bool seek(double timestamp);

        // 获取音频采样格式
        AVSampleFormat getSampleFormat() const { return m_sampleFormat; }

        // 获取音频时长（秒）
        double getDuration() const;

        // 关闭解码器
        void close();

        // 获取错误信息
        std::string getErrorString() const { return m_errorString; }

    private:
        // 初始化Filter Graph
        bool initFilterGraph(double baseVolume, const VolumeMixEffect* effect, double trackDurationSeconds);

        // FFmpeg资源
        AVFormatContext *m_formatContext;
        AVCodecContext *m_codecContext;
        int m_audioStreamIndex;
        struct SwrContext *m_swrCtx;
        
        // Filter graph 资源
        AVFilterGraph *m_filterGraph;
        AVFilterContext *m_bufferSrcCtx;
        AVFilterContext *m_bufferSinkCtx;
        bool m_effectsEnabled = false; // 是否启用效果

        // 音频信息
        int m_sampleRate;
        int m_channels;
        AVSampleFormat m_sampleFormat;
        int64_t m_duration;

        std::string m_errorString;

        // 清理资源
        void cleanup();
    };

} // namespace VideoCreator

#endif // AUDIO_DECODER_H
