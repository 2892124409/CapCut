# VideoCreatorCpp - 基于FFmpeg的视频合成器

这是一个基于 FFmpeg 的 C++ 视频合成器示例项目，支持通过 JSON 配置将图片、音频与转场组合成视频，并能应用简单特效（如 Ken Burns、音量淡入/淡出）。

## 项目结构（简要）

```
VideoCreatorCpp/
├── CMakeLists.txt              # CMake 构建脚本
├── test_config.json            # 示例配置文件（项目使用该文件名）
├── assets/                     # 资源文件目录（图片/音频）
├── src/
│   ├── main.cpp                # 程序入口
│   ├── model/                  # 配置数据结构与解析
│   ├── ffmpeg_utils/           # FFmpeg RAII 辅助封装
│   ├── filter/                 # 滤镜图构建与特效
│   └── engine/                 # 渲染引擎与流程控制
└── 3rdparty/                   # 第三方依赖（如 FFmpeg）
```

## 功能概览

- 支持图片场景与转场（JSON 驱动）
- 支持音频解码并应用简单音量淡入/淡出
- 使用 FFmpeg 编码输出视频（默认 H.264 + AAC）
- 基于 RAII 的 AVFrame/AVPacket 包装器，减少内存泄漏风险

## 构建说明

### 要求

- CMake 3.16+
- 已编译的 FFmpeg（项目中有 `3rdparty/ffmpeg` 示例）
- 支持 C++17 的编译器（MinGW/MSVC 等）

### 构建示例（在 PowerShell 或 bash 中）

```powershell
# 在仓库根目录下
mkdir build; cd build
cmake ..
cmake --build .

# 运行（在 Windows 下可直接运行可执行文件）
.\VideoCreatorCpp
```

注意：`CMakeLists.txt` 会把 `test_config.json`（若存在）和 `assets/` 复制到构建输出目录，便于运行时读取资源。

## 配置说明（`test_config.json`）

程序在 `main.cpp` 中默认尝试加载 `test_config.json`。配置格式与程序中 `ProjectConfig`、`SceneConfig` 等结构对应，示例：

```json
{
  "project": {
    "name": "示例项目",
    "output_path": "output/demo_video.mp4",
    "width": 1280,
    "height": 720,
    "fps": 30,
    "background_color": "#000000"
  },
  "scenes": [
    {
      "type": "image_scene",
      "duration": 5.0,
      "resources": {
        "image": { "path": "assets/shot1.png" },
        "audio": { "path": "assets/music1.mp3" }
      },
      "effects": {
        "ken_burns": { "enabled": true, "start_scale": 1.0, "end_scale": 1.1 },
        "volume_mix": { "enabled": true, "fade_in": 0.5, "fade_out": 0.5 }
      }
    },
    {
      "type": "transition",
      "transition_type": "crossfade",
      "duration": 1.0
    }
  ],
  "global_effects": {
    "video_encoding": { "codec": "libx264", "bitrate": "5000k", "preset": "medium", "crf": 23 },
    "audio_encoding": { "codec": "aac", "bitrate": "192k", "channels": 2 }
  }
}
```

说明要点：
- `project.output_path`：输出文件路径（相对或绝对）
- 每个场景 `type` 可为 `image_scene` 或 `transition`
- 图像资源由 `resources.image.path` 指定，音频由 `resources.audio.path` 指定
- Ken Burns 特效通过 `effects.ken_burns` 控制（程序当前支持 start/end scale 与坐标）

## 运行与调试

- 将资源放到 `assets/`，编辑 `test_config.json` 指向这些资源。
- 构建完成后，将 `test_config.json` 和 `assets/` 内容复制到可执行文件同目录（CMake 已尝试自动复制）。
- 运行程序并观察控制台日志，若遇到 FFmpeg 相关错误，可查看错误输出并确保 `3rdparty/ffmpeg/bin` 下的 DLL 可用或链接正确的静态库。

## 常见问题

- 如果找不到编码器（例如 `libx264`），请确认 FFmpeg 构建包含该编码器，或在 `test_config.json` 中改用系统可用编码器名称。
- 若音频出现杂音/静音，说明输入音频格式需要转换，项目内已实现基于 `swr_convert` 的格式标准化（输出为交错 float）。

## 许可证

MIT License
