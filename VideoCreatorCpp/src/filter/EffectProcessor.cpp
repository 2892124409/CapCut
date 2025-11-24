#include "EffectProcessor.h"
#include <sstream>
#include <cmath>
#include <algorithm>

namespace VideoCreator
{

    EffectProcessor::EffectProcessor()
        : m_filterGraph(nullptr), m_buffersrcContext(nullptr), m_buffersrcContext2(nullptr), m_buffersinkContext(nullptr),
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

        KenBurnsEffect localEffect = effect; // Make a mutable copy

        // Handle presets
        if (!localEffect.preset.empty()) {
            float pan_scale = 1.1f; // Scale for panning to have some room
            if (localEffect.preset == "zoom_in") {
                localEffect.start_scale = 1.0;
                localEffect.end_scale = 1.2;
                localEffect.start_x = 0;
                localEffect.start_y = 0;
                localEffect.end_x = - (inputFrame->width * (localEffect.end_scale - 1.0)) / 2;
                localEffect.end_y = - (inputFrame->height * (localEffect.end_scale - 1.0)) / 2;
            } else if (localEffect.preset == "zoom_out") {
                localEffect.start_scale = 1.2;
                localEffect.end_scale = 1.0;
                localEffect.start_x = - (inputFrame->width * (localEffect.start_scale - 1.0)) / 2;
                localEffect.start_y = - (inputFrame->height * (localEffect.start_scale - 1.0)) / 2;
                localEffect.end_x = 0;
                localEffect.end_y = 0;
            } else if (localEffect.preset == "pan_right") {
                localEffect.start_scale = pan_scale;
                localEffect.end_scale = pan_scale;
                localEffect.start_x = 0;
                localEffect.end_x = - (inputFrame->width * pan_scale - inputFrame->width);
                localEffect.start_y = - (inputFrame->height * pan_scale - inputFrame->height) / 2;
                localEffect.end_y = localEffect.start_y;
            } else if (localEffect.preset == "pan_left") {
                localEffect.start_scale = pan_scale;
                localEffect.end_scale = pan_scale;
                localEffect.start_x = - (inputFrame->width * pan_scale - inputFrame->width);
                localEffect.end_x = 0;
                localEffect.start_y = - (inputFrame->height * pan_scale - inputFrame->height) / 2;
                localEffect.end_y = localEffect.start_y;
            }
        }

        std::stringstream ss;
        double start_x = localEffect.start_x;
        double start_y = localEffect.start_y;
        double start_scale = localEffect.start_scale > 0 ? localEffect.start_scale : 1.0;

        double end_x = localEffect.end_x;
        double end_y = localEffect.end_y;
        double end_scale = localEffect.end_scale > 0 ? localEffect.end_scale : 1.0;

        // Linear interpolation for current state
        double current_scale = start_scale + (end_scale - start_scale) * progress;
        double current_x = start_x + (end_x - start_x) * progress;
        double current_y = start_y + (end_y - start_y) * progress;
        
        // The format for zoompan is `zoompan=z='<zoom>':x='<x>':y='<y>':d=<duration>:s=<size>`
        ss << "zoompan=z=" << current_scale
           << ":x=" << current_x
           << ":y=" << current_y
           << ":d=1:s=" << m_width << "x" << m_height;

        if (!initFilterGraph(ss.str())) {
            return nullptr;
        }

        // Push the input frame into the filter graph
        if (av_buffersrc_add_frame_flags(m_buffersrcContext, (AVFrame*)inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            m_errorString = "Error while feeding the filtergraph";
            return nullptr;
        }

        // Pull the filtered frame from the filter graph
        auto filteredFrame = FFmpegUtils::createAvFrame();
        int ret = av_buffersink_get_frame(m_buffersinkContext, filteredFrame.get());
        if (ret < 0) {
            m_errorString = "Error while receiving a frame from the filtergraph";
            return nullptr;
        }
        
        filteredFrame->pts = inputFrame->pts;

        return filteredFrame;
    }


    FFmpegUtils::AvFramePtr EffectProcessor::applyCrossfade(const AVFrame *fromFrame, const AVFrame *toFrame, double progress)
    {
        if (!fromFrame || !toFrame)
        {
            return nullptr;
        }

        auto outputFrame = FFmpegUtils::createAvFrame(m_width, m_height, m_pixelFormat);
        if (!outputFrame)
        {
            m_errorString = "创建输出帧失败";
            return nullptr;
        }

        outputFrame->format = m_pixelFormat;
        outputFrame->width = m_width;
        outputFrame->height = m_height;

        for (int plane = 0; plane < 3; ++plane)
        {
            int width = (plane == 0) ? m_width : m_width / 2;
            int height = (plane == 0) ? m_height : m_height / 2;

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    uint8_t fromPixel = fromFrame->data[plane][y * fromFrame->linesize[plane] + x];
                    uint8_t toPixel = toFrame->data[plane][y * toFrame->linesize[plane] + x];
                    uint8_t blendedPixel = static_cast<uint8_t>(fromPixel * (1.0 - progress) + toPixel * progress);
                    outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = blendedPixel;
                }
            }
        }

        return outputFrame;
    }

    FFmpegUtils::AvFramePtr EffectProcessor::applyWipe(const AVFrame *fromFrame, const AVFrame *toFrame, double progress)
    {
         if (!fromFrame || !toFrame) return nullptr;
        auto outputFrame = FFmpegUtils::createAvFrame(m_width, m_height, m_pixelFormat);
        if (!outputFrame) return nullptr;

        int wipePosition = static_cast<int>(m_width * progress);

        for (int plane = 0; plane < 3; ++plane)
        {
            int width = (plane == 0) ? m_width : m_width / 2;
            int height = (plane == 0) ? m_height : m_height / 2;
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = (x < wipePosition) ? toFrame->data[plane][y * toFrame->linesize[plane] + x] : fromFrame->data[plane][y * fromFrame->linesize[plane] + x];
                }
            }
        }
        return outputFrame;
    }

    FFmpegUtils::AvFramePtr EffectProcessor::applySlide(const AVFrame *fromFrame, const AVFrame *toFrame, double progress)
    {
        if (!fromFrame || !toFrame) return nullptr;
        auto outputFrame = FFmpegUtils::createAvFrame(m_width, m_height, m_pixelFormat);
        if (!outputFrame) return nullptr;

        int slideOffset = static_cast<int>(m_width * progress);
        for (int plane = 0; plane < 3; ++plane)
        {
            int width = (plane == 0) ? m_width : m_width / 2;
            int height = (plane == 0) ? m_height : m_height / 2;
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    if (x < (m_width - slideOffset)) { // Area for the fromFrame
                        outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = fromFrame->data[plane][y * fromFrame->linesize[plane] + x + slideOffset];
                    } else { // Area for the toFrame
                        outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = toFrame->data[plane][y * toFrame->linesize[plane] + x - (m_width - slideOffset)];
                    }
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
        if (!effect.enabled || audioData.empty()) return audioData;
        std::vector<uint8_t> result = audioData;
        float volume = 1.0f;
        if (progress < effect.fade_in) {
            volume = static_cast<float>(progress / effect.fade_in);
        } else if (progress > (1.0 - effect.fade_out)) {
            volume = static_cast<float>((1.0 - progress) / effect.fade_out);
        }
        float *floatData = reinterpret_cast<float *>(result.data());
        size_t sampleCount = result.size() / sizeof(float);
        for (size_t i = 0; i < sampleCount; ++i) {
            floatData[i] *= volume;
        }
        return result;
    }

    void EffectProcessor::close()
    {
        cleanup();
    }

    void EffectProcessor::cleanup()
    {
        if (m_filterGraph) {
            avfilter_graph_free(&m_filterGraph);
            m_filterGraph = nullptr;
        }
        m_buffersrcContext = nullptr;
        m_buffersinkContext = nullptr;
        m_buffersrcContext2 = nullptr;
    }

    bool EffectProcessor::initFilterGraph(const std::string &filterDescription)
    {
        cleanup(); 
        const AVFilter *buffersrc = avfilter_get_by_name("buffer");
        const AVFilter *buffersink = avfilter_get_by_name("buffersink");
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs = avfilter_inout_alloc();
        m_filterGraph = avfilter_graph_alloc();

        if (!outputs || !inputs || !m_filterGraph) {
            m_errorString = "无法分配滤镜图资源";
            goto end;
        }

        char args[512];
        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                m_width, m_height, m_pixelFormat, 1, 25, 1, 1);

        if (avfilter_graph_create_filter(&m_buffersrcContext, buffersrc, "in", args, nullptr, m_filterGraph) < 0) {
            m_errorString = "无法创建buffer source滤镜";
            goto end;
        }
        if (avfilter_graph_create_filter(&m_buffersinkContext, buffersink, "out", nullptr, nullptr, m_filterGraph) < 0) {
            m_errorString = "无法创建buffer sink滤镜";
            goto end;
        }
        
        outputs->name = av_strdup("in");
        outputs->filter_ctx = m_buffersrcContext;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = m_buffersinkContext;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        if (avfilter_graph_parse_ptr(m_filterGraph, filterDescription.c_str(), &inputs, &outputs, nullptr) < 0) {
            m_errorString = "解析滤镜描述失败: " + filterDescription;
            goto end;
        }

        if (avfilter_graph_config(m_filterGraph, nullptr) < 0) {
            m_errorString = "配置滤镜图失败";
            goto end;
        }

    end:
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        if (!m_errorString.empty()) {
            cleanup();
            return false;
        }
        return true;
    }

} // namespace VideoCreator