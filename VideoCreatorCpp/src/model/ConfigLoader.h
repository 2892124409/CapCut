#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include "ProjectConfig.h"

namespace VideoCreator
{

    class ConfigLoader
    {
    public:
        ConfigLoader() = default;

        // 从JSON文件加载配置
        bool loadFromFile(const QString &filePath, ProjectConfig &config);

        // 从JSON字符串加载配置
        bool loadFromString(const QString &jsonString, ProjectConfig &config);

        // 获取错误信息
        QString errorString() const { return m_errorString; }

    private:
        QString m_errorString;

        // 解析项目配置
        bool parseProjectConfig(const QJsonObject &json, ProjectInfoConfig &project);

        // 解析场景配置
        bool parseSceneConfig(const QJsonObject &json, SceneConfig &scene);

        // 解析资源配置
        bool parseResourcesConfig(const QJsonObject &json, ResourcesConfig &resources);

        bool parseVideoConfig(const QJsonObject &json, VideoConfig &video);

        // 解析图片配置
        bool parseImageConfig(const QJsonObject &json, ImageConfig &image);

        // 解析音频配置
        bool parseAudioConfig(const QJsonObject &json, AudioConfig &audio);

        // 解析特效配置
        bool parseEffectsConfig(const QJsonObject &json, EffectsConfig &effects);

        // 解析Ken Burns特效
        bool parseKenBurnsEffect(const QJsonObject &json, KenBurnsEffect &effect);

        // 解析音量混合特效
        bool parseVolumeMixEffect(const QJsonObject &json, VolumeMixEffect &effect);

        // 解析全局效果配置
        bool parseGlobalEffectsConfig(const QJsonObject &json, GlobalEffectsConfig &global_effects);

        // 解析音频标准化配置
        bool parseAudioNormalizationConfig(const QJsonObject &json, AudioNormalizationConfig &config);

        // 解析视频编码配置
        bool parseVideoEncodingConfig(const QJsonObject &json, VideoEncodingConfig &config);

        // 解析音频编码配置
        bool parseAudioEncodingConfig(const QJsonObject &json, AudioEncodingConfig &config);

        // 获取音频文件时长（秒）
        double getAudioDuration(const std::string &audioPath);
        double getVideoDuration(const std::string &videoPath);

        // 字符串到枚举转换
        SceneType stringToSceneType(const QString &typeStr);
        TransitionType stringToTransitionType(const QString &typeStr);
    };

} // namespace VideoCreator

#endif // CONFIG_LOADER_H
