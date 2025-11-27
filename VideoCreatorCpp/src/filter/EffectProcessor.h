#ifndef EFFECT_PROCESSOR_H
#define EFFECT_PROCESSOR_H

#include <string>
#include <vector>
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

        bool initialize(int width, int height, AVPixelFormat format, int fps);

        // Pre-renders the entire Ken Burns effect sequence using a time-variant expression.
        bool processKenBurnsEffect(const KenBurnsEffect& effect, const AVFrame* inputImage, int total_frames);

        // Gets a pre-rendered frame from the cache.
        const AVFrame* getKenBurnsFrame(int frame_index) const;

        // Transition effects
        FFmpegUtils::AvFramePtr applyCrossfade(const AVFrame* fromFrame, const AVFrame* toFrame, int frame_index, int duration_frames);
        FFmpegUtils::AvFramePtr applyWipe(const AVFrame* fromFrame, const AVFrame* toFrame, int frame_index, int duration_frames);
        FFmpegUtils::AvFramePtr applySlide(const AVFrame* fromFrame, const AVFrame* toFrame, int frame_index, int duration_frames);

        std::string getErrorString() const { return m_errorString; }
        void close();

    private:
        AVFilterGraph *m_filterGraph;
        AVFilterContext *m_buffersrcContext;
        AVFilterContext* m_buffersrcContext2;
        AVFilterContext *m_buffersinkContext;

        int m_width;
        int m_height;
        AVPixelFormat m_pixelFormat;
        int m_fps;
        mutable std::string m_errorString;

        // Frame cache for the pre-rendered Ken Burns effect
        std::vector<FFmpegUtils::AvFramePtr> m_kb_frames;
        bool m_kb_enabled;

        // Frame cache for transitions
        std::vector<FFmpegUtils::AvFramePtr> m_transition_frames;
        bool m_transition_initialized = false;

        // Private methods
        bool initFilterGraph(const std::string &filterDescription);
        bool initTransitionFilterGraph(const std::string& filter_description);
        bool processTransition(const AVFrame* fromFrame, const AVFrame* toFrame, const std::string& transitionName, int duration_frames);
        void cleanup();
    };

} // namespace VideoCreator

#endif // EFFECT_PROCESSOR_H