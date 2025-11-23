#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <string>
#include <vector>

namespace VideoCreator {

// 资源类型枚举
enum class ResourceType {
    IMAGE,
    VIDEO,
    AUDIO
};

// 特效类型枚举
enum class EffectType {
    NONE,
    FADE_IN,
    FADE_OUT,
    ZOOM,
    PAN,
    ROTATE
};

// 资源配置
struct ResourceConfig {
    std::string path;
    ResourceType type;
    double start_time;  // 开始时间（秒）
    double duration;    // 持续时间（秒）
    double volume;      // 音量（0.0-1.0）
};

// 特效配置
struct EffectConfig {
    EffectType type;
    double start_time;  // 开始时间（秒）
    double duration;    // 持续时间（秒）
    std::string params; // 特效参数
};

// 场景配置
struct SceneConfig {
    std::string name;
    double duration;    // 场景持续时间（秒）
    std::vector<ResourceConfig> resources;
    std::vector<EffectConfig> effects;
};

// 输出配置
struct OutputConfig {
    std::string output_path;
    int width;
    int height;
    int frame_rate;
    int video_bitrate;  // 视频码率（bps）
    int audio_bitrate;  // 音频码率（bps）
    std::string video_codec;
    std::string audio_codec;
};

// 项目全局配置
struct ProjectConfig {
    std::string project_name;
    OutputConfig output;
    std::vector<SceneConfig> scenes;
    
    // 默认构造函数
    ProjectConfig() {
        output.width = 1920;
        output.height = 1080;
        output.frame_rate = 30;
        output.video_bitrate = 4000000; // 4Mbps
        output.audio_bitrate = 128000;  // 128kbps
        output.video_codec = "libx264";
        output.audio_codec = "aac";
    }
};

} // namespace VideoCreator

#endif // PROJECT_CONFIG_H
