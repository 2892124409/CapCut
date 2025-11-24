#include "EffectProcessor.h"
#include <sstream>
#include <cmath>
#include <algorithm>

namespace VideoCreator
{

    EffectProcessor::EffectProcessor()
        : m_width(0), m_height(0), m_pixelFormat(AV_PIX_FMT_NONE)
    {
    }

    EffectProcessor::~EffectProcessor()
    {
    }

    bool EffectProcessor::initialize(int width, int height, AVPixelFormat format)
    {
        m_width = width;
        m_height = height;
        m_pixelFormat = format;
        return true;
    }

    FFmpegUtils::AvFramePtr EffectProcessor::applyKenBurns(const AVFrame *inputFrame,
                                                           const KenBurnsEffect &effect,
                                                           double progress)
    {
        if (!inputFrame || !effect.enabled)
        {
            return FFmpegUtils::copyAvFrame(inputFrame);
        }

        // 简化实现：直接返回原图，不应用Ken Burns特效
        // 在实际项目中，这里应该实现真正的Ken Burns缩放和平移动画
        return FFmpegUtils::copyAvFrame(inputFrame);
    }

    FFmpegUtils::AvFramePtr EffectProcessor::applyCrossfade(const AVFrame *fromFrame, const AVFrame *toFrame, double progress)
    {
        if (!fromFrame || !toFrame)
        {
            return nullptr;
        }

        // 创建输出帧
        auto outputFrame = FFmpegUtils::createAvFrame(m_width, m_height, m_pixelFormat);
        if (!outputFrame)
        {
            m_errorString = "创建输出帧失败";
            return nullptr;
        }

        // 确保输出帧的像素格式正确设置
        outputFrame->format = m_pixelFormat;
        outputFrame->width = m_width;
        outputFrame->height = m_height;

        // 简单的交叉淡入淡出实现
        // 注意：这里假设两个帧的格式和尺寸相同
        for (int plane = 0; plane < 3; ++plane)
        {
            int width = m_width;
            int height = m_height;
            
            if (plane > 0)
            {
                width = m_width / 2;
                height = m_height / 2;
            }

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    uint8_t fromPixel = fromFrame->data[plane][y * fromFrame->linesize[plane] + x];
                    uint8_t toPixel = toFrame->data[plane][y * toFrame->linesize[plane] + x];
                    
                    // 线性插值
                    uint8_t blendedPixel = static_cast<uint8_t>(
                        fromPixel * (1.0 - progress) + toPixel * progress
                    );
                    
                    outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = blendedPixel;
                }
            }
        }

        return outputFrame;
    }

    FFmpegUtils::AvFramePtr EffectProcessor::applyWipe(const AVFrame *fromFrame, const AVFrame *toFrame, double progress)
    {
        if (!fromFrame || !toFrame)
        {
            return nullptr;
        }

        // 创建输出帧
        auto outputFrame = FFmpegUtils::createAvFrame(m_width, m_height, m_pixelFormat);
        if (!outputFrame)
        {
            m_errorString = "创建输出帧失败";
            return nullptr;
        }

        // 简单的擦除转场实现
        int wipePosition = static_cast<int>(m_width * progress);

        for (int plane = 0; plane < 3; ++plane)
        {
            int width = m_width;
            int height = m_height;
            
            if (plane > 0)
            {
                width = m_width / 2;
                height = m_height / 2;
            }

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    uint8_t pixel;
                    if (x < wipePosition)
                    {
                        // 使用toFrame
                        pixel = toFrame->data[plane][y * toFrame->linesize[plane] + x];
                    }
                    else
                    {
                        // 使用fromFrame
                        pixel = fromFrame->data[plane][y * fromFrame->linesize[plane] + x];
                    }
                    
                    outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = pixel;
                }
            }
        }

        return outputFrame;
    }

    FFmpegUtils::AvFramePtr EffectProcessor::applySlide(const AVFrame *fromFrame, const AVFrame *toFrame, double progress)
    {
        if (!fromFrame || !toFrame)
        {
            return nullptr;
        }

        // 创建输出帧
        auto outputFrame = FFmpegUtils::createAvFrame(m_width, m_height, m_pixelFormat);
        if (!outputFrame)
        {
            m_errorString = "创建输出帧失败";
            return nullptr;
        }

        // 简单的滑动转场实现
        int slideOffset = static_cast<int>(m_width * progress);

        for (int plane = 0; plane < 3; ++plane)
        {
            int width = m_width;
            int height = m_height;
            
            if (plane > 0)
            {
                width = m_width / 2;
                height = m_height / 2;
            }

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    uint8_t pixel;
                    if (x < slideOffset)
                    {
                        // 使用toFrame的右侧部分
                        int sourceX = x + (width - slideOffset);
                        if (sourceX >= width) sourceX = width - 1;
                        pixel = toFrame->data[plane][y * toFrame->linesize[plane] + sourceX];
                    }
                    else
                    {
                        // 使用fromFrame的左侧部分
                        int sourceX = x - slideOffset;
                        if (sourceX < 0) sourceX = 0;
                        pixel = fromFrame->data[plane][y * fromFrame->linesize[plane] + sourceX];
                    }
                    
                    outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = pixel;
                }
            }
        }

        return outputFrame;
    }

    std::vector<uint8_t> EffectProcessor::applyVolumeMix(const std::vector<uint8_t> &audioData,
                                                         const VolumeMixEffect &effect,
                                                         double progress,
                                                         int sampleRate, int channels)
    {
        if (!effect.enabled || audioData.empty())
        {
            return audioData;
        }

        std::vector<uint8_t> result = audioData;
        float volume = 1.0f;

        // 计算淡入淡出效果
        if (progress < effect.fade_in)
        {
            // 淡入阶段
            volume = static_cast<float>(progress / effect.fade_in);
        }
        else if (progress > (1.0 - effect.fade_out))
        {
            // 淡出阶段
            volume = static_cast<float>((1.0 - progress) / effect.fade_out);
        }

        // 应用音量到音频数据 (简化处理，直接应用音量系数)
        float *floatData = reinterpret_cast<float *>(result.data());
        size_t sampleCount = result.size() / sizeof(float);

        for (size_t i = 0; i < sampleCount; ++i)
        {
            floatData[i] *= volume;
        }

        return result;
    }

    void EffectProcessor::close()
    {
        // 简化实现，不需要清理
    }

    void EffectProcessor::cleanup()
    {
        // 简化实现，不需要清理
    }

} // namespace VideoCreator
