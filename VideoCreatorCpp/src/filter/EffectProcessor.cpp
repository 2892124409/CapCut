#include "EffectProcessor.h"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <locale>
#include <iomanip> // For std::setprecision
#include <atomic>

namespace VideoCreator
{

    // Use an atomic integer for a thread-safe, unique ID for filter instances.
    static std::atomic<int> filter_instance_count(0);

    EffectProcessor::EffectProcessor()
        : m_filterGraph(nullptr), m_buffersrcContext(nullptr), m_buffersinkContext(nullptr),
          m_width(0), m_height(0), m_pixelFormat(AV_PIX_FMT_NONE), m_fps(0), m_kb_enabled(false)
    {
    }

    EffectProcessor::~EffectProcessor()
    {
        cleanup();
    }

    bool EffectProcessor::initialize(int width, int height, AVPixelFormat format, int fps)
    {
        m_width = width;
        m_height = height;
        m_pixelFormat = format;
        m_fps = fps;
        return true;
    }

    bool EffectProcessor::processKenBurnsEffect(const KenBurnsEffect& effect, const AVFrame* inputImage, int total_frames)
    {
        m_kb_enabled = effect.enabled;
        if (!m_kb_enabled) {
            return true;
        }
        if (!inputImage) {
            m_errorString = "Input image for Ken Burns effect is null.";
            return false;
        }
        if (total_frames <= 0) {
            return true; 
        }

        KenBurnsEffect params = effect;

        std::stringstream ss;
        ss.imbue(std::locale("C"));

        if (params.preset == "zoom_in" || params.preset == "zoom_out")
        {
            double start_z = (params.preset == "zoom_in") ? 1.0 : 1.2;
            double end_z = (params.preset == "zoom_in") ? 1.2 : 1.0;
            
            std::stringstream zoom_ss;
            zoom_ss << std::fixed << std::setprecision(10) << start_z << "+(" << (end_z - start_z) << ")*on/" << total_frames;
            std::string zoom_expr = zoom_ss.str();

            ss << "zoompan="
               << "z='" << zoom_expr << "':"
               << "d=" << total_frames << ":s=" << m_width << "x" << m_height << ":fps=" << m_fps;
        }
        else if (params.preset == "pan_right" || params.preset == "pan_left")
        {
            float pan_scale = 1.1f;
            double start_x, end_x, start_y;

            if (params.preset == "pan_right") {
                start_x = 0;
                end_x = m_width * (pan_scale - 1.0);
            } else { // pan_left
                start_x = m_width * (pan_scale - 1.0);
                end_x = 0;
            }
            start_y = (m_height * (pan_scale - 1.0)) / 2;
            
            ss << "zoompan="
               << "z='" << pan_scale << "':" // Zoom is constant for panning
               << "x='" << start_x << "+(" << end_x - start_x << ")*on/" << total_frames << "':"
               << "y='" << start_y << "':" // Y is constant for horizontal panning
               << "d=" << total_frames << ":s=" << m_width << "x" << m_height << ":fps=" << m_fps;
        }
        else 
        {
            // Fallback for custom ken_burns or empty preset from config
            ss << "zoompan="
               << "z='" << params.start_scale << "+(" << params.end_scale - params.start_scale << ")*on/" << total_frames << "':"
               << "x='" << params.start_x << "+(" << params.end_x - params.start_x << ")*on/" << total_frames << "':"
               << "y='" << params.start_y << "+(" << params.end_y - params.start_y << ")*on/" << total_frames << "':"
               << "d=" << total_frames << ":s=" << m_width << "x" << m_height << ":fps=" << m_fps;
        }

        if (!initFilterGraph(ss.str())) {
            return false;
        }

        AVFrame* src_frame = av_frame_clone(inputImage);
        if (!src_frame) {
            m_errorString = "Failed to clone source image for filter.";
            return false;
        }
        src_frame->pts = 0; // The timestamp for the single input image should be 0

        if (av_buffersrc_add_frame(m_buffersrcContext, src_frame) < 0) {
            m_errorString = "Error while feeding the source image to the filtergraph";
            av_frame_free(&src_frame);
            return false;
        }
        av_frame_free(&src_frame);

        if (av_buffersrc_add_frame(m_buffersrcContext, nullptr) < 0) {
             m_errorString = "Failed to signal EOF to Ken Burns filter source.";
            return false;
        }

        m_kb_frames.clear();
        for (int i = 0; i < total_frames; ++i) {
            auto filteredFrame = FFmpegUtils::createAvFrame();
            int ret = av_buffersink_get_frame(m_buffersinkContext, filteredFrame.get());
            if (ret < 0) {
                if (ret == AVERROR_EOF) break; 
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                m_errorString = "Error while receiving a frame from the filtergraph: " + std::string(errbuf);
                return false;
            }
            m_kb_frames.push_back(std::move(filteredFrame));
        }

        if (m_kb_frames.size() != total_frames) {
             m_errorString = "Generated frame count (" + std::to_string(m_kb_frames.size()) + ") does not match total_frames (" + std::to_string(total_frames) + ").";
             return false;
        }

        return true;
    }

    const AVFrame* EffectProcessor::getKenBurnsFrame(int frame_index) const
    {
        if (!m_kb_enabled) {
            m_errorString = "Ken Burns effect is not enabled or processed.";
            return nullptr;
        }
        if (frame_index < 0 || frame_index >= m_kb_frames.size()) {
            m_errorString = "Frame index out of bounds for cached Ken Burns frames.";
            return nullptr;
        }
        return m_kb_frames[frame_index].get();
    }

    FFmpegUtils::AvFramePtr EffectProcessor::applyCrossfade(const AVFrame *fromFrame, const AVFrame *toFrame, double progress)
    {
        if (!fromFrame || !toFrame) return nullptr;
        auto outputFrame = FFmpegUtils::createAvFrame(m_width, m_height, m_pixelFormat);
        if (!outputFrame) { m_errorString = "创建输出帧失败"; return nullptr; }

        for (int plane = 0; plane < 3; ++plane) {
            int width = (plane == 0) ? m_width : m_width / 2;
            int height = (plane == 0) ? m_height : m_height / 2;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    uint8_t fromPixel = fromFrame->data[plane][y * fromFrame->linesize[plane] + x];
                    uint8_t toPixel = toFrame->data[plane][y * toFrame->linesize[plane] + x];
                    outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = static_cast<uint8_t>(fromPixel * (1.0 - progress) + toPixel * progress);
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
        for (int plane = 0; plane < 3; ++plane) {
            int width = (plane == 0) ? m_width : m_width / 2;
            int height = (plane == 0) ? m_height : m_height / 2;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
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
        for (int plane = 0; plane < 3; ++plane) {
            int width = (plane == 0) ? m_width : m_width / 2;
            int height = (plane == 0) ? m_height : m_height / 2;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    if (x < (m_width - slideOffset)) {
                        outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = fromFrame->data[plane][y * fromFrame->linesize[plane] + x + slideOffset];
                    } else {
                        outputFrame->data[plane][y * outputFrame->linesize[plane] + x] = toFrame->data[plane][y * toFrame->linesize[plane] + x - (m_width - slideOffset)];
                    }
                }
            }
        }
        return outputFrame;
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
        m_kb_frames.clear();
    }

    bool EffectProcessor::initFilterGraph(const std::string &filterDescription)
    {
        cleanup(); 
        const AVFilter *buffersrc = avfilter_get_by_name("buffer");
        const AVFilter *buffersink = avfilter_get_by_name("buffersink");
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs = avfilter_inout_alloc();
        char args[512];
        char instance_name_in[32], instance_name_out[32];
        int instance_id;
        std::string fullFilterDesc;

        m_filterGraph = avfilter_graph_alloc();

        if (!outputs || !inputs || !m_filterGraph) {
            m_errorString = "无法分配滤镜图资源";
            goto end;
        }

        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=1/%d:pixel_aspect=%d/%d",
                m_width, m_height, m_pixelFormat, m_fps, 1, 1);

        instance_id = filter_instance_count++;
        snprintf(instance_name_in, sizeof(instance_name_in), "buffer_src_%d", instance_id);
        snprintf(instance_name_out, sizeof(instance_name_out), "buffer_sink_%d", instance_id);

        if (avfilter_graph_create_filter(&m_buffersrcContext, buffersrc, instance_name_in, args, nullptr, m_filterGraph) < 0) {
            m_errorString = "无法创建buffer source滤镜";
            goto end;
        }
        if (avfilter_graph_create_filter(&m_buffersinkContext, buffersink, instance_name_out, nullptr, nullptr, m_filterGraph) < 0) {
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

        fullFilterDesc = "[in]" + filterDescription + "[out]";

        if (avfilter_graph_parse_ptr(m_filterGraph, fullFilterDesc.c_str(), &inputs, &outputs, nullptr) < 0) {
            m_errorString = "解析滤镜描述失败: " + fullFilterDesc;
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