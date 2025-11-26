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
        FFmpegUtils::AvFramePtr applyCrossfade(const AVFrame *fromFrame, const AVFrame *toFrame, double progress);
        FFmpegUtils::AvFramePtr applyWipe(const AVFrame *fromFrame, const AVFrame *toFrame, double progress);
        FFmpegUtils::AvFramePtr applySlide(const AVFrame *fromFrame, const AVFrame *toFrame, double progress);

        std::string getErrorString() const { return m_errorString; }
        void close();

    private:
        AVFilterGraph *m_filterGraph;
        AVFilterContext *m_buffersrcContext;
        AVFilterContext *m_buffersinkContext;

        int m_width;
        int m_height;
        AVPixelFormat m_pixelFormat;
        int m_fps;
        mutable std::string m_errorString;

        // Frame cache for the pre-rendered Ken Burns effect
        std::vector<FFmpegUtils::AvFramePtr> m_kb_frames;
        bool m_kb_enabled;

        // Private methods
        bool initFilterGraph(const std::string &filterDescription);
        void cleanup();
    };

} // namespace VideoCreator

#endif // EFFECT_PROCESSOR_H