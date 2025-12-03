#include "VideoCreatorAPI.h"

#include <mutex>
#include <QString>

#include "model/ConfigLoader.h"
#include "engine/RenderEngine.h"
#include "ffmpeg_utils/FFmpegHeaders.h"

namespace VideoCreator
{
    namespace
    {
        bool ensureFFmpegInitialized(std::string *error)
        {
            static std::once_flag initFlag;
            static int initResult = 0;
            static std::string initError;

            std::call_once(initFlag, [&]() {
                initResult = avformat_network_init();
                if (initResult < 0)
                {
                    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                    av_strerror(initResult, errbuf, sizeof(errbuf));
                    initError = std::string("FFmpeg network init failed: ") + errbuf;
                }
            });

            if (initResult < 0)
            {
                if (error)
                {
                    *error = initError;
                }
                return false;
            }
            return true;
        }

        bool renderWithConfig(const ProjectConfig &config, std::string *error)
        {
            RenderEngine engine;
            if (!engine.initialize(config))
            {
                if (error)
                {
                    *error = engine.errorString();
                }
                return false;
            }
            if (!engine.render())
            {
                if (error)
                {
                    *error = engine.errorString();
                }
                return false;
            }
            return true;
        }
    } // namespace

    bool RenderFromJson(const std::string &config_path, std::string *error)
    {
        if (!ensureFFmpegInitialized(error))
        {
            return false;
        }

        ConfigLoader loader;
        ProjectConfig config;
        if (!loader.loadFromFile(QString::fromStdString(config_path), config))
        {
            if (error)
            {
                *error = loader.errorString().toStdString();
            }
            return false;
        }

        return renderWithConfig(config, error);
    }

    bool RenderFromJsonString(const std::string &json_string, std::string *error)
    {
        if (!ensureFFmpegInitialized(error))
        {
            return false;
        }

        ConfigLoader loader;
        ProjectConfig config;
        if (!loader.loadFromString(QString::fromStdString(json_string), config))
        {
            if (error)
            {
                *error = loader.errorString().toStdString();
            }
            return false;
        }

        return renderWithConfig(config, error);
    }
} // namespace VideoCreator
