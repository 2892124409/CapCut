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

        bool initialize(int width, int height, AVPixelFormat format, int fps);

        // Ken Burns streaming helpers
        bool startKenBurnsSequence(const KenBurnsEffect& effect, const AVFrame* inputImage, int total_frames);
        bool fetchKenBurnsFrame(FFmpegUtils::AvFramePtr &outFrame);

        // Transition streaming helpers
        bool startTransitionSequence(TransitionType type, const AVFrame* fromFrame, const AVFrame* toFrame, int duration_frames);
        bool fetchTransitionFrame(FFmpegUtils::AvFramePtr &outFrame);

        std::string getErrorString() const { return m_errorString; }
        void close();

    private:
        enum class SequenceType
        {
            None,
            KenBurns,
            Transition
        };

        AVFilterGraph *m_filterGraph;
        AVFilterContext *m_buffersrcContext;
        AVFilterContext* m_buffersrcContext2;
        AVFilterContext *m_buffersinkContext;

        int m_width;
        int m_height;
        AVPixelFormat m_pixelFormat;
        int m_fps;
        mutable std::string m_errorString;

        SequenceType m_sequenceType;
        int m_expectedFrames;
        int m_generatedFrames;

        bool initFilterGraph(const std::string &filterDescription);
        bool initTransitionFilterGraph(const std::string& filter_description);
        bool retrieveFrame(FFmpegUtils::AvFramePtr &outFrame);
        void stampFrameColorInfo(AVFrame *frame) const;
        void resetSequenceState();
        void cleanup();
    };

} // namespace VideoCreator

#endif // EFFECT_PROCESSOR_H
