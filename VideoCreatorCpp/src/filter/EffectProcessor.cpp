#include "EffectProcessor.h"
#include <sstream>
#include <cmath>

namespace VideoCreator
{

    EffectProcessor::EffectProcessor()
        : m_filterGraph(nullptr), m_buffersrcContext(nullptr), m_buffersinkContext(nullptr),
          m_width(0), m_height(0), m_pixelFormat(AV_PIX_FMT_NONE)
    {
    }

    EffectProcessor::~EffectProcessor()
    {
        cleanup();
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

        // 创建滤镜描述字符串
        std::string filterDesc = createKenBurnsFilterString(effect, progress);

        // 初始化滤镜图
        if (!initFilterGraph(filterDesc))
        {
            return nullptr;
        }

        // 发送输入帧到滤镜
        if (av_buffersrc_add_frame(m_buffersrcContext, const_cast<AVFrame *>(inputFrame)) < 0)
        {
            m_errorString = "发送帧到滤镜失败";
            return nullptr;
        }

        // 从滤镜接收处理后的帧
        auto outputFrame = FFmpegUtils::createAvFrame(m_width, m_height, m_pixelFormat);
        if (!outputFrame)
        {
            m_errorString = "创建输出帧失败";
            return nullptr;
        }

        int ret = av_buffersink_get_frame(m_buffersinkContext, outputFrame.get());
        if (ret < 0)
        {
            if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            {
                m_errorString = "从滤镜接收帧失败";
            }
            return nullptr;
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

    bool EffectProcessor::initFilterGraph(const std::string &filterDescription)
    {
        cleanup();

        m_filterGraph = avfilter_graph_alloc();
        if (!m_filterGraph)
        {
            m_errorString = "无法分配滤镜图";
            return false;
        }

        // 创建buffer源滤镜
        const AVFilter *buffersrc = avfilter_get_by_name("buffer");
        if (!buffersrc)
        {
            m_errorString = "无法找到buffer滤镜";
            cleanup();
            return false;
        }

        std::stringstream args;
        args << "video_size=" << m_width << "x" << m_height << ":pix_fmt=" << m_pixelFormat
             << ":time_base=1/30:pixel_aspect=1/1";

        if (avfilter_graph_create_filter(&m_buffersrcContext, buffersrc, "in",
                                         args.str().c_str(), nullptr, m_filterGraph) < 0)
        {
            m_errorString = "无法创建buffer源滤镜";
            cleanup();
            return false;
        }

        // 创建buffer sink滤镜
        const AVFilter *buffersink = avfilter_get_by_name("buffersink");
        if (!buffersink)
        {
            m_errorString = "无法找到buffersink滤镜";
            cleanup();
            return false;
        }

        if (avfilter_graph_create_filter(&m_buffersinkContext, buffersink, "out",
                                         nullptr, nullptr, m_filterGraph) < 0)
        {
            m_errorString = "无法创建buffer sink滤镜";
            cleanup();
            return false;
        }

        // 解析滤镜图
        AVFilterInOut *inputs = avfilter_inout_alloc();
        AVFilterInOut *outputs = avfilter_inout_alloc();

        inputs->name = av_strdup("out");
        inputs->filter_ctx = m_buffersinkContext;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        outputs->name = av_strdup("in");
        outputs->filter_ctx = m_buffersrcContext;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        if (avfilter_graph_parse_ptr(m_filterGraph, filterDescription.c_str(),
                                     &inputs, &outputs, nullptr) < 0)
        {
            m_errorString = "无法解析滤镜图";
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            cleanup();
            return false;
        }

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        // 配置滤镜图
        if (avfilter_graph_config(m_filterGraph, nullptr) < 0)
        {
            m_errorString = "无法配置滤镜图";
            cleanup();
            return false;
        }

        return true;
    }

    std::string EffectProcessor::createKenBurnsFilterString(const KenBurnsEffect &effect, double progress)
    {
        std::stringstream filter;

        // 计算当前缩放比例
        double currentScale = effect.start_scale + (effect.end_scale - effect.start_scale) * progress;

        // 计算当前位置
        double currentX = effect.start_x + (effect.end_x - effect.start_x) * progress;
        double currentY = effect.start_y + (effect.end_y - effect.start_y) * progress;

        // 创建zoompan滤镜字符串
        filter << "zoompan=z='min(zoom+" << (currentScale - 1.0) * 0.1 << "+0.0005, " << currentScale << ")':"
               << "x='iw/2-(iw/zoom/2)':"
               << "y='ih/2-(ih/zoom/2)':"
               << "d=1:s=" << m_width << "x" << m_height;

        return filter.str();
    }

    void EffectProcessor::close()
    {
        cleanup();
    }

    void EffectProcessor::cleanup()
    {
        if (m_filterGraph)
        {
            avfilter_graph_free(&m_filterGraph);
            m_filterGraph = nullptr;
            m_buffersrcContext = nullptr;
            m_buffersinkContext = nullptr;
        }
    }

} // namespace VideoCreator