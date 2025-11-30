#include "ConfigLoader.h"
#include <QDebug>
#include <QProcess>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace VideoCreator
{

    bool ConfigLoader::loadFromFile(const QString &filePath, ProjectConfig &config)
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            m_errorString = QString("无法打开配置文件: %1").arg(filePath);
            return false;
        }

        QByteArray jsonData = file.readAll();
        file.close();

        return loadFromString(QString::fromUtf8(jsonData), config);
    }

    bool ConfigLoader::loadFromString(const QString &jsonString, ProjectConfig &config)
    {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);

        if (parseError.error != QJsonParseError::NoError)
        {
            m_errorString = QString("JSON解析错误: %1").arg(parseError.errorString());
            return false;
        }

        if (!doc.isObject())
        {
            m_errorString = "JSON根元素不是对象";
            return false;
        }

        QJsonObject root = doc.object();

        // 解析项目配置
        if (root.contains("project") && root["project"].isObject())
        {
            if (!parseProjectConfig(root["project"].toObject(), config.project))
            {
                return false;
            }
        }

        // 解析场景配置
        if (root.contains("scenes") && root["scenes"].isArray())
        {
            QJsonArray scenesArray = root["scenes"].toArray();
            config.scenes.clear();

            int sceneId = 1; // 从1开始分配场景ID
            for (const QJsonValue &sceneValue : scenesArray)
            {
                if (sceneValue.isObject())
                {
                    SceneConfig scene;
                    scene.id = sceneId; // 自动分配场景ID
                    if (parseSceneConfig(sceneValue.toObject(), scene))
                    {
                        config.scenes.push_back(scene);
                        sceneId++;
                    }
                    else
                    {
                        return false;
                    }
                }
            }
        }

        // 解析全局效果配置
        if (root.contains("global_effects") && root["global_effects"].isObject())
        {
            if (!parseGlobalEffectsConfig(root["global_effects"].toObject(), config.global_effects))
            {
                return false;
            }
        }

        return true;
    }

    bool ConfigLoader::parseProjectConfig(const QJsonObject &json, ProjectInfoConfig &project)
    {
        if (json.contains("name") && json["name"].isString())
        {
            project.name = json["name"].toString().toUtf8().toStdString();
        }

        if (json.contains("output_path") && json["output_path"].isString())
        {
            project.output_path = json["output_path"].toString().toUtf8().toStdString();
        }

        if (json.contains("width") && json["width"].isDouble())
        {
            project.width = json["width"].toInt();
        }

        if (json.contains("height") && json["height"].isDouble())
        {
            project.height = json["height"].toInt();
        }

        if (json.contains("fps") && json["fps"].isDouble())
        {
            project.fps = json["fps"].toInt();
        }

        if (json.contains("background_color") && json["background_color"].isString())
        {
            project.background_color = json["background_color"].toString().toUtf8().toStdString();
        }

        return true;
    }

    bool ConfigLoader::parseSceneConfig(const QJsonObject &json, SceneConfig &scene)
    {
        if (json.contains("id") && json["id"].isDouble())
        {
            scene.id = json["id"].toInt();
        }

        if (json.contains("type") && json["type"].isString())
        {
            scene.type = stringToSceneType(json["type"].toString());
        }

        // 解析资源配置
        if (json.contains("resources") && json["resources"].isObject())
        {
            if (!parseResourcesConfig(json["resources"].toObject(), scene.resources))
            {
                return false;
            }
        }

        // 解析特效配置
        if (json.contains("effects") && json["effects"].isObject())
        {
            if (!parseEffectsConfig(json["effects"].toObject(), scene.effects))
            {
                return false;
            }
        }

        // 解析转场相关字段
        if (json.contains("transition_type") && json["transition_type"].isString())
        {
            scene.transition_type = stringToTransitionType(json["transition_type"].toString());
        }

        if (json.contains("from_scene") && json["from_scene"].isDouble())
        {
            scene.from_scene = json["from_scene"].toInt();
        }

        if (json.contains("to_scene") && json["to_scene"].isDouble())
        {
            scene.to_scene = json["to_scene"].toInt();
        }

        // 如果JSON中没有指定duration，则根据音频时长自动计算
        if (json.contains("duration") && json["duration"].isDouble())
        {
            scene.duration = json["duration"].toDouble();
        }
        else if (scene.type == SceneType::IMAGE_SCENE && !scene.resources.audio.path.empty())
        {
            // 自动从音频文件获取时长
            double audioDuration = getAudioDuration(scene.resources.audio.path);
            if (audioDuration > 0)
            {
                scene.duration = audioDuration;
                qDebug() << "自动设置场景时长为音频时长:" << audioDuration << "秒";
            }
            else
            {
                // 如果无法获取音频时长，使用默认值5秒
                scene.duration = 5.0;
                qDebug() << "无法获取音频时长，使用默认时长: 5秒";
            }
        }
        else if (scene.type == SceneType::VIDEO_SCENE && !scene.resources.video.path.empty())
        {
            double videoDuration = getVideoDuration(scene.resources.video.path);
            if (videoDuration > 0)
            {
                scene.duration = videoDuration;
                qDebug() << "自动设置视频场景时长:" << videoDuration << "秒";
            }
            else
            {
                scene.duration = 5.0;
                qDebug() << "无法获取视频时长，使用默认时长: 5秒";
            }
        }

        else if (scene.type == SceneType::IMAGE_SCENE)
        {
            // 没有音频的场景，使用默认时长5秒
            scene.duration = 5.0;
            qDebug() << "场景没有音频，使用默认时长: 5秒";
        }

        else if (scene.type == SceneType::VIDEO_SCENE)
        {
            scene.duration = 5.0;
            qDebug() << "视频场景缺少资源，使用默认时长: 5秒";
        }

        return true;
    }

    bool ConfigLoader::parseResourcesConfig(const QJsonObject &json, ResourcesConfig &resources)
    {
        // 解析图片配置
        if (json.contains("image") && json["image"].isObject())
        {
            if (!parseImageConfig(json["image"].toObject(), resources.image))
            {
                return false;
            }
        }

        // 解析视频配置
        if (json.contains("video") && json["video"].isObject())
        {
            if (!parseVideoConfig(json["video"].toObject(), resources.video))
            {
                return false;
            }
        }

        // 解析音频配置
        if (json.contains("audio") && json["audio"].isObject())
        {
            if (!parseAudioConfig(json["audio"].toObject(), resources.audio))
            {
                return false;
            }
        }

        return true;
    }

    bool ConfigLoader::parseImageConfig(const QJsonObject &json, ImageConfig &image)
    {
        if (json.contains("path") && json["path"].isString())
        {
            image.path = json["path"].toString().toUtf8().toStdString();
        }

        if (json.contains("position") && json["position"].isObject())
        {
            QJsonObject position = json["position"].toObject();
            if (position.contains("x") && position["x"].isDouble())
            {
                image.x = position["x"].toInt();
            }
            if (position.contains("y") && position["y"].isDouble())
            {
                image.y = position["y"].toInt();
            }
        }

        if (json.contains("scale") && json["scale"].isDouble())
        {
            image.scale = json["scale"].toDouble();
        }

        if (json.contains("rotation") && json["rotation"].isDouble())
        {
            image.rotation = json["rotation"].toDouble();
        }

        return true;
    }

    bool ConfigLoader::parseAudioConfig(const QJsonObject &json, AudioConfig &audio)
    {
        if (json.contains("path") && json["path"].isString())
        {
            audio.path = json["path"].toString().toUtf8().toStdString();
        }

        if (json.contains("volume") && json["volume"].isDouble())
        {
            audio.volume = json["volume"].toDouble();
        }

        if (json.contains("start_offset") && json["start_offset"].isDouble())
        {
            audio.start_offset = json["start_offset"].toDouble();
        }

        return true;
    }

    bool ConfigLoader::parseVideoConfig(const QJsonObject &json, VideoConfig &video)
    {
        if (json.contains("path") && json["path"].isString())
        {
            video.path = json["path"].toString().toUtf8().toStdString();
        }

        if (json.contains("trim_start") && json["trim_start"].isDouble())
        {
            video.trim_start = json["trim_start"].toDouble();
        }

        if (json.contains("trim_end") && json["trim_end"].isDouble())
        {
            video.trim_end = json["trim_end"].toDouble();
        }

        if (json.contains("use_audio") && json["use_audio"].isBool())
        {
            video.use_audio = json["use_audio"].toBool();
        }

        return true;
    }

    bool ConfigLoader::parseEffectsConfig(const QJsonObject &json, EffectsConfig &effects)
    {
        // 解析Ken Burns特效
        if (json.contains("ken_burns") && json["ken_burns"].isObject())
        {
            if (!parseKenBurnsEffect(json["ken_burns"].toObject(), effects.ken_burns))
            {
                return false;
            }
        }

        // 解析音量混合特效
        if (json.contains("volume_mix") && json["volume_mix"].isObject())
        {
            if (!parseVolumeMixEffect(json["volume_mix"].toObject(), effects.volume_mix))
            {
                return false;
            }
        }

        return true;
    }

    bool ConfigLoader::parseKenBurnsEffect(const QJsonObject &json, KenBurnsEffect &effect)
    {
        if (json.contains("enabled") && json["enabled"].isBool())
        {
            effect.enabled = json["enabled"].toBool();
        }

        if (json.contains("preset") && json["preset"].isString())
        {
            effect.preset = json["preset"].toString().toStdString();
        }

        if (json.contains("start_scale") && json["start_scale"].isDouble())
        {
            effect.start_scale = json["start_scale"].toDouble();
        }

        if (json.contains("end_scale") && json["end_scale"].isDouble())
        {
            effect.end_scale = json["end_scale"].toDouble();
        }

        if (json.contains("start_x") && json["start_x"].isDouble())
        {
            effect.start_x = json["start_x"].toInt();
        }

        if (json.contains("start_y") && json["start_y"].isDouble())
        {
            effect.start_y = json["start_y"].toInt();
        }

        if (json.contains("end_x") && json["end_x"].isDouble())
        {
            effect.end_x = json["end_x"].toInt();
        }

        if (json.contains("end_y") && json["end_y"].isDouble())
        {
            effect.end_y = json["end_y"].toInt();
        }

        return true;
    }

    bool ConfigLoader::parseVolumeMixEffect(const QJsonObject &json, VolumeMixEffect &effect)
    {
        if (json.contains("enabled") && json["enabled"].isBool())
        {
            effect.enabled = json["enabled"].toBool();
        }

        if (json.contains("fade_in") && json["fade_in"].isDouble())
        {
            effect.fade_in = json["fade_in"].toDouble();
        }

        if (json.contains("fade_out") && json["fade_out"].isDouble())
        {
            effect.fade_out = json["fade_out"].toDouble();
        }

        return true;
    }

    bool ConfigLoader::parseGlobalEffectsConfig(const QJsonObject &json, GlobalEffectsConfig &global_effects)
    {
        // 解析音频标准化配置
        if (json.contains("audio_normalization") && json["audio_normalization"].isObject())
        {
            if (!parseAudioNormalizationConfig(json["audio_normalization"].toObject(), global_effects.audio_normalization))
            {
                return false;
            }
        }

        // 解析视频编码配置
        if (json.contains("video_encoding") && json["video_encoding"].isObject())
        {
            if (!parseVideoEncodingConfig(json["video_encoding"].toObject(), global_effects.video_encoding))
            {
                return false;
            }
        }

        // 解析音频编码配置
        if (json.contains("audio_encoding") && json["audio_encoding"].isObject())
        {
            if (!parseAudioEncodingConfig(json["audio_encoding"].toObject(), global_effects.audio_encoding))
            {
                return false;
            }
        }

        return true;
    }

    bool ConfigLoader::parseAudioNormalizationConfig(const QJsonObject &json, AudioNormalizationConfig &config)
    {
        if (json.contains("enabled") && json["enabled"].isBool())
        {
            config.enabled = json["enabled"].toBool();
        }

        if (json.contains("target_level") && json["target_level"].isDouble())
        {
            config.target_level = json["target_level"].toDouble();
        }

        return true;
    }

    bool ConfigLoader::parseVideoEncodingConfig(const QJsonObject &json, VideoEncodingConfig &config)
    {
        if (json.contains("codec") && json["codec"].isString())
        {
            config.codec = json["codec"].toString().toUtf8().toStdString();
        }

        if (json.contains("bitrate") && json["bitrate"].isString())
        {
            config.bitrate = json["bitrate"].toString().toUtf8().toStdString();
        }

        if (json.contains("preset") && json["preset"].isString())
        {
            config.preset = json["preset"].toString().toUtf8().toStdString();
        }

        if (json.contains("crf") && json["crf"].isDouble())
        {
            config.crf = json["crf"].toInt();
        }

        return true;
    }

    bool ConfigLoader::parseAudioEncodingConfig(const QJsonObject &json, AudioEncodingConfig &config)
    {
        if (json.contains("codec") && json["codec"].isString())
        {
            config.codec = json["codec"].toString().toUtf8().toStdString();
        }

        if (json.contains("bitrate") && json["bitrate"].isString())
        {
            config.bitrate = json["bitrate"].toString().toUtf8().toStdString();
        }

        if (json.contains("channels") && json["channels"].isDouble())
        {
            config.channels = json["channels"].toInt();
        }

        return true;
    }

    SceneType ConfigLoader::stringToSceneType(const QString &typeStr)
    {
        if (typeStr == "image_scene")
            return SceneType::IMAGE_SCENE;
        if (typeStr == "video_scene")
            return SceneType::VIDEO_SCENE;
        if (typeStr == "transition")
            return SceneType::TRANSITION;
        return SceneType::IMAGE_SCENE; // 默认值
    }

    TransitionType ConfigLoader::stringToTransitionType(const QString &typeStr)
    {
        if (typeStr == "crossfade")
            return TransitionType::CROSSFADE;
        if (typeStr == "wipe")
            return TransitionType::WIPE;
        if (typeStr == "slide")
            return TransitionType::SLIDE;
        return TransitionType::CROSSFADE; // 默认值
    }

    double ConfigLoader::getAudioDuration(const std::string &audioPath)
    {
        // 检查路径是否为空
        if (audioPath.empty())
        {
            qDebug() << "音频路径为空";
            return -1.0;
        }

        // 检查文件是否存在
        QFile file(QString::fromStdString(audioPath));
        if (!file.exists())
        {
            qDebug() << "音频文件不存在:" << QString::fromStdString(audioPath);
            return -1.0;
        }

        // 在新版本FFmpeg中，不需要调用av_register_all()
        // FFmpeg库会自动初始化

        AVFormatContext *formatCtx = nullptr;
        int ret = avformat_open_input(&formatCtx, audioPath.c_str(), nullptr, nullptr);

        if (ret < 0)
        {
            // 获取详细的错误信息
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "无法打开音频文件:" << QString::fromStdString(audioPath);
            qDebug() << "错误码:" << ret << "错误信息:" << errbuf;
            return -1.0;
        }

        // 获取流信息
        ret = avformat_find_stream_info(formatCtx, nullptr);
        if (ret < 0)
        {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "无法获取音频流信息:" << QString::fromStdString(audioPath);
            qDebug() << "错误码:" << ret << "错误信息:" << errbuf;
            avformat_close_input(&formatCtx);
            return -1.0;
        }

        // 查找音频流
        int audioStreamIndex = -1;
        for (unsigned int i = 0; i < formatCtx->nb_streams; i++)
        {
            if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                audioStreamIndex = i;
                break;
            }
        }

        if (audioStreamIndex == -1)
        {
            qDebug() << "未找到音频流:" << QString::fromStdString(audioPath);
            avformat_close_input(&formatCtx);
            return -1.0;
        }

        // 获取音频时长（秒）
        double duration = 0.0;
        if (formatCtx->duration != AV_NOPTS_VALUE)
        {
            duration = formatCtx->duration / (double)AV_TIME_BASE;
        }
        else if (formatCtx->streams[audioStreamIndex]->duration != AV_NOPTS_VALUE)
        {
            AVStream *stream = formatCtx->streams[audioStreamIndex];
            duration = stream->duration * av_q2d(stream->time_base);
        }

        avformat_close_input(&formatCtx);

        if (duration > 0)
        {
            qDebug() << "音频时长:" << duration << "秒" << "文件:" << QString::fromStdString(audioPath);
            return duration;
        }

        return -1.0;
    }

    double ConfigLoader::getVideoDuration(const std::string &videoPath)
    {
        if (videoPath.empty())
        {
            qDebug() << "视频路径为空";
            return -1.0;
        }

        QFile file(QString::fromStdString(videoPath));
        if (!file.exists())
        {
            qDebug() << "视频文件不存在:" << QString::fromStdString(videoPath);
            return -1.0;
        }

        AVFormatContext *formatCtx = nullptr;
        int ret = avformat_open_input(&formatCtx, videoPath.c_str(), nullptr, nullptr);

        if (ret < 0)
        {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "无法打开视频文件:" << QString::fromStdString(videoPath);
            qDebug() << "错误码:" << ret << "错误信息:" << errbuf;
            return -1.0;
        }

        ret = avformat_find_stream_info(formatCtx, nullptr);
        if (ret < 0)
        {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qDebug() << "无法获取视频流信息:" << QString::fromStdString(videoPath);
            qDebug() << "错误码:" << ret << "错误信息:" << errbuf;
            avformat_close_input(&formatCtx);
            return -1.0;
        }

        int videoStreamIndex = -1;
        for (unsigned int i = 0; i < formatCtx->nb_streams; i++)
        {
            if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                videoStreamIndex = i;
                break;
            }
        }

        if (videoStreamIndex == -1)
        {
            qDebug() << "未找到视频流:" << QString::fromStdString(videoPath);
            avformat_close_input(&formatCtx);
            return -1.0;
        }

        double duration = 0.0;
        if (formatCtx->duration != AV_NOPTS_VALUE)
        {
            duration = formatCtx->duration / (double)AV_TIME_BASE;
        }
        else if (formatCtx->streams[videoStreamIndex]->duration != AV_NOPTS_VALUE)
        {
            AVStream *stream = formatCtx->streams[videoStreamIndex];
            duration = stream->duration * av_q2d(stream->time_base);
        }

        avformat_close_input(&formatCtx);

        if (duration > 0)
        {
            qDebug() << "视频时长:" << duration << "秒" << "文件:" << QString::fromStdString(videoPath);
            return duration;
        }

        return -1.0;
    }

} // namespace VideoCreator
