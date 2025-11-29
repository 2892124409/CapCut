#include "EffectProcessor.h"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <locale>
#include <iomanip> // For std::setprecision
#include <libavutil/opt.h>
#include <atomic>

namespace VideoCreator
{

    // Use an atomic integer for a thread-safe, unique ID for filter instances.
    static std::atomic<int> filter_instance_count(0);

    EffectProcessor::EffectProcessor()
        : m_filterGraph(nullptr), m_buffersrcContext(nullptr), m_buffersrcContext2(nullptr), m_buffersinkContext(nullptr),
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

        // 统一色彩空间/范围元数据，避免滤镜链输出的帧缺少或使用默认值导致色偏
        AVColorSpace cs = (m_height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        AVColorPrimaries cp = (m_height >= 720) ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
        AVColorTransferCharacteristic ctrc = (m_height >= 720) ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
        AVRational sar{1,1};
        auto stamp_color_info = [cs, cp, ctrc, sar](AVFrame* f) {
            if (!f) return;
            f->color_range = AVCOL_RANGE_MPEG;
            f->colorspace = cs;
            f->color_primaries = cp;
            f->color_trc = ctrc;
            f->sample_aspect_ratio = sar;
        };

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
            stamp_color_info(filteredFrame.get());
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

    FFmpegUtils::AvFramePtr EffectProcessor::applyCrossfade(const AVFrame* fromFrame, const AVFrame* toFrame, int frame_index, int duration_frames)
    {
        if (!processTransition(fromFrame, toFrame, "fade", duration_frames)) {
            return nullptr;
        }
        if (frame_index < 0 || frame_index >= m_transition_frames.size()) {
            m_errorString = "Frame index out of bounds for cached transition frames.";
            return nullptr;
        }
        return FFmpegUtils::copyAvFrame(m_transition_frames[frame_index].get());
    }

    FFmpegUtils::AvFramePtr EffectProcessor::applyWipe(const AVFrame* fromFrame, const AVFrame* toFrame, int frame_index, int duration_frames)
    {
        // xfade supports different wipe patterns like wiperight, wipeleft, wipeup, wipedown
        if (!processTransition(fromFrame, toFrame, "wipeleft", duration_frames)) {
            return nullptr;
        }
        if (frame_index < 0 || frame_index >= m_transition_frames.size()) {
            m_errorString = "Frame index out of bounds for cached transition frames.";
            return nullptr;
        }
        return FFmpegUtils::copyAvFrame(m_transition_frames[frame_index].get());
    }

    FFmpegUtils::AvFramePtr EffectProcessor::applySlide(const AVFrame* fromFrame, const AVFrame* toFrame, int frame_index, int duration_frames)
    {
        // xfade supports various slide patterns
        if (!processTransition(fromFrame, toFrame, "slideleft", duration_frames)) {
            return nullptr;
        }
        if (frame_index < 0 || frame_index >= m_transition_frames.size()) {
            m_errorString = "Frame index out of bounds for cached transition frames.";
            return nullptr;
        }
        return FFmpegUtils::copyAvFrame(m_transition_frames[frame_index].get());
    }

    bool EffectProcessor::processTransition(const AVFrame* fromFrame, const AVFrame* toFrame, const std::string& transitionName, int duration_frames)
    {
        m_transition_frames.clear();
        if (!fromFrame || !toFrame) {
            m_errorString = "Input frames for transition are null.";
            return false;
        }

        double transition_duration_sec = (double)duration_frames / m_fps;

        std::stringstream ss;
        ss.imbue(std::locale("C"));
        ss << std::fixed << std::setprecision(5);
        bool use_bt709 = m_height >= 720;
        
        // Create a filtergraph that pads each single-frame input into a full-duration stream, then xfades them.
        ss << "[in0]tpad=stop_mode=clone:stop_duration=" << transition_duration_sec << "[s0];"
           << "[in1]tpad=stop_mode=clone:stop_duration=" << transition_duration_sec << "[s1];"
           << "[s0][s1]xfade=transition=" << transitionName
           << ":duration=" << transition_duration_sec << ":offset=0"
           << ",format=pix_fmts=yuv420p[out]";

        if (!initTransitionFilterGraph(ss.str())) {
            return false;
        }

        AVFrame* from_clone = av_frame_clone(fromFrame);
        AVFrame* to_clone = av_frame_clone(toFrame);
        if (!from_clone || !to_clone) {
            m_errorString = "Failed to clone frames for transition.";
            av_frame_free(&from_clone);
            av_frame_free(&to_clone);
            return false;
        }
        // Stamp consistent color metadata before feeding into the filter graph
        AVColorSpace cs = use_bt709 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        AVColorPrimaries cp = use_bt709 ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
        AVColorTransferCharacteristic ctrc = use_bt709 ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
        AVRational sar{1,1};

        auto stamp_frame = [&](AVFrame* f){
            if (!f) return;
            f->color_range = AVCOL_RANGE_MPEG;
            f->colorspace = cs;
            f->color_primaries = cp;
            f->color_trc = ctrc;
            f->sample_aspect_ratio = sar;
        };

        stamp_frame(from_clone);
        stamp_frame(to_clone);
        
        from_clone->pts = 0;
        if (av_buffersrc_add_frame_flags(m_buffersrcContext, from_clone, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            m_errorString = "Error feeding 'from' frame to transition filtergraph.";
            av_frame_free(&to_clone); // Free the other clone
            return false;
        }

        to_clone->pts = 0;
        if (av_buffersrc_add_frame_flags(m_buffersrcContext2, to_clone, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            m_errorString = "Error feeding 'to' frame to transition filtergraph.";
            return false;
        }

        // Signal EOF to both inputs. tpad will loop them.
        if (av_buffersrc_add_frame(m_buffersrcContext, nullptr) < 0 || av_buffersrc_add_frame(m_buffersrcContext2, nullptr) < 0) {
            m_errorString = "Failed to signal EOF to transition filter sources.";
            return false;
        }

        for (int i = 0; i < duration_frames; ++i) {
            auto filteredFrame = FFmpegUtils::createAvFrame();
            int ret = av_buffersink_get_frame(m_buffersinkContext, filteredFrame.get());
            if (ret < 0) {
                if (ret == AVERROR_EOF) break;
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, sizeof(errbuf));
                m_errorString = "Error receiving frame from transition filtergraph: " + std::string(errbuf);
                return false;
            }
            stamp_frame(filteredFrame.get());
            m_transition_frames.push_back(std::move(filteredFrame));
        }
        
        if (m_transition_frames.size() != duration_frames) {
             m_errorString = "Generated transition frame count (" + std::to_string(m_transition_frames.size()) + ") does not match duration_frames (" + std::to_string(duration_frames) + ").";
             return false;
        }

        return true;
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
        m_buffersrcContext2 = nullptr;
        m_buffersinkContext = nullptr;
        m_kb_frames.clear();
        m_transition_frames.clear();
    }

    bool EffectProcessor::initFilterGraph(const std::string &filterDescription)
    {
        cleanup();
        const AVFilter *buffersrc = avfilter_get_by_name("buffer");
        const AVFilter *buffersink = avfilter_get_by_name("buffersink");
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs = avfilter_inout_alloc();
        char args[512];
        std::string fullFilterDesc;
        int colorspace;

        m_filterGraph = avfilter_graph_alloc();

        if (!outputs || !inputs || !m_filterGraph) {
            m_errorString = "无法分配滤镜图资源";
            goto end;
        }

        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=1/%d:pixel_aspect=%d/%d:frame_rate=%d/1",
                m_width, m_height, m_pixelFormat, m_fps, 1, 1, m_fps);

        if (avfilter_graph_create_filter(&m_buffersrcContext, buffersrc, "in", args, nullptr, m_filterGraph) < 0) {
            m_errorString = "无法创建buffer source滤镜";
            goto end;
        }
        
        // Set color properties directly on the filter context to support older FFmpeg versions
        colorspace = (m_height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        av_opt_set_int(m_buffersrcContext, "color_range", AVCOL_RANGE_MPEG, 0);
        av_opt_set_int(m_buffersrcContext, "colorspace", colorspace, 0);
        av_opt_set_int(m_buffersrcContext, "color_primaries", (m_height >= 720) ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M, 0);
        av_opt_set_int(m_buffersrcContext, "color_trc", (m_height >= 720) ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M, 0);
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

    bool EffectProcessor::initTransitionFilterGraph(const std::string& filter_description)
    {
        cleanup();
        const AVFilter* buffersrc = avfilter_get_by_name("buffer");
        const AVFilter* buffersink = avfilter_get_by_name("buffersink");
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();
        char args[512];
        int colorspace;

        m_filterGraph = avfilter_graph_alloc();
        if (!outputs || !inputs || !m_filterGraph) {
            m_errorString = "Cannot allocate filter graph resources.";
            goto end;
        }
        snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=1/%d:pixel_aspect=%d/%d:frame_rate=%d/1", m_width, m_height, m_pixelFormat, m_fps, 1, 1, m_fps);

        if (avfilter_graph_create_filter(&m_buffersrcContext, buffersrc, "in0", args, nullptr, m_filterGraph) < 0) {
            m_errorString = "Cannot create buffer source 0.";
            goto end;
        }
        if (avfilter_graph_create_filter(&m_buffersrcContext2, buffersrc, "in1", args, nullptr, m_filterGraph) < 0) {
            m_errorString = "Cannot create buffer source 1.";
            goto end;
        }

        // Set color properties directly on the filter contexts to support older FFmpeg versions
        colorspace = (m_height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        av_opt_set_int(m_buffersrcContext, "color_range", AVCOL_RANGE_MPEG, 0);
        av_opt_set_int(m_buffersrcContext, "colorspace", colorspace, 0);
        av_opt_set_int(m_buffersrcContext, "color_primaries", (m_height >= 720) ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M, 0);
        av_opt_set_int(m_buffersrcContext, "color_trc", (m_height >= 720) ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M, 0);
        av_opt_set_int(m_buffersrcContext2, "color_range", AVCOL_RANGE_MPEG, 0);
        av_opt_set_int(m_buffersrcContext2, "colorspace", colorspace, 0);
        av_opt_set_int(m_buffersrcContext2, "color_primaries", (m_height >= 720) ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M, 0);
        av_opt_set_int(m_buffersrcContext2, "color_trc", (m_height >= 720) ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M, 0);
        if (avfilter_graph_create_filter(&m_buffersinkContext, buffersink, "out", nullptr, nullptr, m_filterGraph) < 0) {
            m_errorString = "Cannot create buffer sink.";
            goto end;
        }

        outputs->name = av_strdup("in0");
        outputs->filter_ctx = m_buffersrcContext;
        outputs->pad_idx = 0;
        outputs->next = avfilter_inout_alloc();
        if (!outputs->next) {
            av_free(outputs->name);
            m_errorString = "Cannot allocate inout for second input.";
            goto end;
        }

        outputs->next->name = av_strdup("in1");
        outputs->next->filter_ctx = m_buffersrcContext2;
        outputs->next->pad_idx = 0;
        outputs->next->next = nullptr;
        
        inputs->name = av_strdup("out");
        inputs->filter_ctx = m_buffersinkContext;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        if (avfilter_graph_parse_ptr(m_filterGraph, filter_description.c_str(), &inputs, &outputs, nullptr) < 0) {
            m_errorString = "Failed to parse filter description: " + filter_description;
            goto end;
        }

        if (avfilter_graph_config(m_filterGraph, nullptr) < 0) {
            m_errorString = "Failed to configure filter graph.";
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
