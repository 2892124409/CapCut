#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <string>
#include <memory>
#include <vector>
#include "ffmpeg_utils/FFmpegHeaders.h"
#include "ffmpeg_utils/AvFrameWrapper.h"

namespace VideoCreator
{

    class AudioDecoder
    {
    public:
        AudioDecoder();
        ~AudioDecoder();

        // 打开音频文件
        bool open(const std::string &filePath);

        // 解码音频数据
        std::vector<uint8_t> decode();

        // 获取音频信息
        int getSampleRate() const { return m_sampleRate; }
        int getChannels() const { return m_channels; }
        AVSampleFormat getSampleFormat() const { return m_sampleFormat; }
        int64_t getDuration() const { return m_duration; }

        // 关闭解码器
        void close();

        // 获取错误信息
        std::string getErrorString() const { return m_errorString; }

    private:
        // FFmpeg资源
        AVFormatContext *m_formatContext;
        AVCodecContext *m_codecContext;
        int m_audioStreamIndex;

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