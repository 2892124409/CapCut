#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <string>
#include <vector>

namespace VideoCreator
{

    // 场景类型枚举
    enum class SceneType
    {
        IMAGE_SCENE,
        VIDEO_SCENE,
        TRANSITION
    };

    // 转场类型枚举
    enum class TransitionType
    {
        CROSSFADE,
        WIPE,
        SLIDE
    };

    // 辅助函数：将 TransitionType 转换为字符串
    inline std::string transitionTypeToString(TransitionType type)
    {
        switch (type)
        {
        case TransitionType::CROSSFADE:
            return "CROSSFADE";
        case TransitionType::WIPE:
            return "WIPE";
        case TransitionType::SLIDE:
            return "SLIDE";
        default:
            return "UNKNOWN";
        }
    }

    // 图片配置
    struct ImageConfig
    {
        std::string path;      // 图片文件路径
        int x = 0;             // X坐标
        int y = 0;             // Y坐标
        double scale = 1.0;    // 缩放比例
        double rotation = 0.0; // 旋转角度
    };

    // 音频配置
    struct AudioConfig
    {
        std::string path;          // 音频文件路径
        double volume = 1.0;       // 音量
        double start_offset = 0.0; // 开始偏移时间
    };

    // 视频配置
    struct VideoConfig
    {
        std::string path;       // 视频文件路径
        double trim_start = 0.0; // 起始偏移
        double trim_end = -1.0;  // 结束时间（-1 表示使用全长）
        bool use_audio = true;   // 是否使用原视频音频
    };

    // 资源配置
    struct ResourcesConfig
    {
        ImageConfig image; // 图片配置
        AudioConfig audio; // 音频配置
        VideoConfig video; // 视频配置
        std::vector<AudioConfig> audio_layers; // Additional audio layers for mixing
    };

    // Ken Burns特效配置
    struct KenBurnsEffect
    {
        bool enabled = false;     // 是否启用
        std::string preset;       // 预设名称, e.g., "zoom_in"
        double start_scale = 1.0; // 开始缩放
        double end_scale = 1.0;   // 结束缩放
        int start_x = 0;          // 开始X坐标
        int start_y = 0;          // 开始Y坐标
        int end_x = 0;            // 结束X坐标
        int end_y = 0;            // 结束Y坐标
    };

    // 音量混合特效配置
    struct VolumeMixEffect
    {
        bool enabled = false;  // 是否启用
        double fade_in = 0.0;  // 淡入时间
        double fade_out = 0.0; // 淡出时间
    };

    // 特效配置
    struct EffectsConfig
    {
        KenBurnsEffect ken_burns;   // Ken Burns特效
        VolumeMixEffect volume_mix; // 音量混合特效
    };

    // 场景配置
    struct SceneConfig
    {
        int id = 0;                              // 场景ID
        SceneType type = SceneType::IMAGE_SCENE; // 场景类型
        double duration = 0.0;                   // 持续时间
        ResourcesConfig resources;               // 资源配置
        EffectsConfig effects;                   // 特效配置

        // 转场相关字段
        TransitionType transition_type = TransitionType::CROSSFADE; // 转场类型
        int from_scene = 0;                                         // 起始场景ID
        int to_scene = 0;                                           // 目标场景ID
    };

    // 音频标准化配置
    struct AudioNormalizationConfig
    {
        bool enabled = false;        // 是否启用
        double target_level = -16.0; // 目标音量级别(dB)
    };

    // 视频编码配置
    struct VideoEncodingConfig
    {
        std::string codec = "libx264"; // 编码器
        std::string bitrate = "5000k"; // 比特率
        std::string preset = "medium"; // 预设
        int crf = 23;                  // 质量因子
    };

    // 音频编码配置
    struct AudioEncodingConfig
    {
        std::string codec = "aac";    // 编码器
        std::string bitrate = "192k"; // 比特率
        int channels = 2;             // 声道数
    };

    // 全局效果配置
    struct GlobalEffectsConfig
    {
        AudioNormalizationConfig audio_normalization; // 音频标准化
        VideoEncodingConfig video_encoding;           // 视频编码
        AudioEncodingConfig audio_encoding;           // 音频编码
    };

    // 项目基本信息配置
    struct ProjectInfoConfig
    {
        std::string name;                         // 项目名称
        std::string output_path;                  // 输出路径
        int width = 1920;                         // 视频宽度
        int height = 1080;                        // 视频高度
        int fps = 30;                             // 帧率
        std::string background_color = "#000000"; // 背景颜色
    };

    // 项目全局配置
    struct ProjectConfig
    {
        ProjectInfoConfig project;          // 项目基本信息
        std::vector<SceneConfig> scenes;    // 场景列表
        GlobalEffectsConfig global_effects; // 全局效果配置

        // 默认构造函数
        ProjectConfig()
        {
            project.width = 1920;
            project.height = 1080;
            project.fps = 30;
            project.background_color = "#000000";
        }
    };

} // namespace VideoCreator

#endif // PROJECT_CONFIG_H
