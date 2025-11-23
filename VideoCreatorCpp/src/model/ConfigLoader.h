#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include "ProjectConfig.h"

namespace VideoCreator {

class ConfigLoader {
public:
    ConfigLoader() = default;
    
    // 从JSON文件加载配置
    bool loadFromFile(const QString& filePath, ProjectConfig& config);
    
    // 从JSON字符串加载配置
    bool loadFromString(const QString& jsonString, ProjectConfig& config);
    
    // 获取错误信息
    QString errorString() const { return m_errorString; }

private:
    QString m_errorString;
    
    // 解析输出配置
    bool parseOutputConfig(const QJsonObject& json, OutputConfig& output);
    
    // 解析场景配置
    bool parseSceneConfig(const QJsonObject& json, SceneConfig& scene);
    
    // 解析资源配置
    bool parseResourceConfig(const QJsonObject& json, ResourceConfig& resource);
    
    // 解析特效配置
    bool parseEffectConfig(const QJsonObject& json, EffectConfig& effect);
    
    // 字符串到枚举转换
    ResourceType stringToResourceType(const QString& typeStr);
    EffectType stringToEffectType(const QString& typeStr);
};

} // namespace VideoCreator

#endif // CONFIG_LOADER_H
