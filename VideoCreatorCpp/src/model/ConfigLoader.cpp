#include "ConfigLoader.h"
#include <QDebug>

namespace VideoCreator {

bool ConfigLoader::loadFromFile(const QString& filePath, ProjectConfig& config) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_errorString = QString("无法打开配置文件: %1").arg(filePath);
        return false;
    }
    
    QByteArray jsonData = file.readAll();
    file.close();
    
    return loadFromString(QString::fromUtf8(jsonData), config);
}

bool ConfigLoader::loadFromString(const QString& jsonString, ProjectConfig& config) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        m_errorString = QString("JSON解析错误: %1").arg(parseError.errorString());
        return false;
    }
    
    if (!doc.isObject()) {
        m_errorString = "JSON根元素不是对象";
        return false;
    }
    
    QJsonObject root = doc.object();
    
    // 解析项目名称
    if (root.contains("project_name") && root["project_name"].isString()) {
        config.project_name = root["project_name"].toString().toUtf8().toStdString();
    }
    
    // 解析输出配置
    if (root.contains("output") && root["output"].isObject()) {
        if (!parseOutputConfig(root["output"].toObject(), config.output)) {
            return false;
        }
    }
    
    // 解析场景配置
    if (root.contains("scenes") && root["scenes"].isArray()) {
        QJsonArray scenesArray = root["scenes"].toArray();
        config.scenes.clear();
        
        for (const QJsonValue& sceneValue : scenesArray) {
            if (sceneValue.isObject()) {
                SceneConfig scene;
                if (parseSceneConfig(sceneValue.toObject(), scene)) {
                    config.scenes.push_back(scene);
                } else {
                    return false;
                }
            }
        }
    }
    
    return true;
}

bool ConfigLoader::parseOutputConfig(const QJsonObject& json, OutputConfig& output) {
    if (json.contains("output_path") && json["output_path"].isString()) {
        output.output_path = json["output_path"].toString().toUtf8().toStdString();
    }
    
    if (json.contains("width") && json["width"].isDouble()) {
        output.width = json["width"].toInt();
    }
    
    if (json.contains("height") && json["height"].isDouble()) {
        output.height = json["height"].toInt();
    }
    
    if (json.contains("frame_rate") && json["frame_rate"].isDouble()) {
        output.frame_rate = json["frame_rate"].toInt();
    }
    
    if (json.contains("video_bitrate") && json["video_bitrate"].isDouble()) {
        output.video_bitrate = json["video_bitrate"].toInt();
    }
    
    if (json.contains("audio_bitrate") && json["audio_bitrate"].isDouble()) {
        output.audio_bitrate = json["audio_bitrate"].toInt();
    }
    
    if (json.contains("video_codec") && json["video_codec"].isString()) {
        output.video_codec = json["video_codec"].toString().toUtf8().toStdString();
    }
    
    if (json.contains("audio_codec") && json["audio_codec"].isString()) {
        output.audio_codec = json["audio_codec"].toString().toUtf8().toStdString();
    }
    
    return true;
}

bool ConfigLoader::parseSceneConfig(const QJsonObject& json, SceneConfig& scene) {
    if (json.contains("name") && json["name"].isString()) {
        scene.name = json["name"].toString().toUtf8().toStdString();
    }
    
    if (json.contains("duration") && json["duration"].isDouble()) {
        scene.duration = json["duration"].toDouble();
    }
    
    // 解析资源
    if (json.contains("resources") && json["resources"].isArray()) {
        QJsonArray resourcesArray = json["resources"].toArray();
        scene.resources.clear();
        
        for (const QJsonValue& resourceValue : resourcesArray) {
            if (resourceValue.isObject()) {
                ResourceConfig resource;
                if (parseResourceConfig(resourceValue.toObject(), resource)) {
                    scene.resources.push_back(resource);
                } else {
                    return false;
                }
            }
        }
    }
    
    // 解析特效
    if (json.contains("effects") && json["effects"].isArray()) {
        QJsonArray effectsArray = json["effects"].toArray();
        scene.effects.clear();
        
        for (const QJsonValue& effectValue : effectsArray) {
            if (effectValue.isObject()) {
                EffectConfig effect;
                if (parseEffectConfig(effectValue.toObject(), effect)) {
                    scene.effects.push_back(effect);
                } else {
                    return false;
                }
            }
        }
    }
    
    return true;
}

bool ConfigLoader::parseResourceConfig(const QJsonObject& json, ResourceConfig& resource) {
    if (json.contains("path") && json["path"].isString()) {
        resource.path = json["path"].toString().toUtf8().toStdString();
    }
    
    if (json.contains("type") && json["type"].isString()) {
        resource.type = stringToResourceType(json["type"].toString());
    }
    
    if (json.contains("start_time") && json["start_time"].isDouble()) {
        resource.start_time = json["start_time"].toDouble();
    }
    
    if (json.contains("duration") && json["duration"].isDouble()) {
        resource.duration = json["duration"].toDouble();
    }
    
    if (json.contains("volume") && json["volume"].isDouble()) {
        resource.volume = json["volume"].toDouble();
    }
    
    return true;
}

bool ConfigLoader::parseEffectConfig(const QJsonObject& json, EffectConfig& effect) {
    if (json.contains("type") && json["type"].isString()) {
        effect.type = stringToEffectType(json["type"].toString());
    }
    
    if (json.contains("start_time") && json["start_time"].isDouble()) {
        effect.start_time = json["start_time"].toDouble();
    }
    
    if (json.contains("duration") && json["duration"].isDouble()) {
        effect.duration = json["duration"].toDouble();
    }
    
    if (json.contains("params") && json["params"].isString()) {
        effect.params = json["params"].toString().toUtf8().toStdString();
    }
    
    return true;
}

ResourceType ConfigLoader::stringToResourceType(const QString& typeStr) {
    if (typeStr == "IMAGE") return ResourceType::IMAGE;
    if (typeStr == "VIDEO") return ResourceType::VIDEO;
    if (typeStr == "AUDIO") return ResourceType::AUDIO;
    return ResourceType::IMAGE; // 默认值
}

EffectType ConfigLoader::stringToEffectType(const QString& typeStr) {
    if (typeStr == "FADE_IN") return EffectType::FADE_IN;
    if (typeStr == "FADE_OUT") return EffectType::FADE_OUT;
    if (typeStr == "ZOOM") return EffectType::ZOOM;
    if (typeStr == "PAN") return EffectType::PAN;
    if (typeStr == "ROTATE") return EffectType::ROTATE;
    return EffectType::NONE; // 默认值
}

} // namespace VideoCreator
