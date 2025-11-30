# VideoCreatorCpp - 基于FFmpeg的视频合成器

这是一个基于 FFmpeg 的 C++ 视频合成器项目，支持通过 JSON 配置将图片、音频与转场组合成视频，并能应用 Ken Burns（推拉摇移）、交叉淡化等视觉特效。

## 功能概览

- **JSON驱动**: 通过 `test_config.json` 文件灵活定义视频的每一个场景和转场。
- **音画同步**: 实现了精确的音视频同步机制，确保画面与声音完美对齐。
- **动态场景时长**: 场景时长可由其关联的音频文件长度自动决定，优先于在JSON中指定的 `duration`。
- **视频片段拼接**: 新增 `video_scene` 类型，可顺序拼接多个视频文件，并可选择复用原始音轨或覆盖独立配音。
- **视频特效**: 支持 Ken Burns（推拉摇移）特效，可通过预设 (`preset`) 或具体坐标参数进行配置。
- **转场效果**: 支持场景间的交叉淡化（crossfade）、擦除（wipe）和滑动（slide）转场。
- **标准编码输出**: 使用 FFmpeg 将视频编码为 H.264，音频编码为 AAC，并封装在 MP4 容器中。
- **健壮的音频处理**: 内置音频重采样和 FIFO 缓冲机制，以处理不同格式的音频并满足编码器的严格要求。
- **进度报告**: 渲染过程中会在控制台输出实时进度。

## 核心工作流程

项目的工作流程可以概括为以下几个步骤：

1.  **配置加载 (`ConfigLoader`)**:
    *   程序启动，`ConfigLoader` 读取并解析 `test_config.json` 文件，将其内容映射到 C++ 的 `ProjectConfig` 结构体中。

2.  **渲染引擎初始化 (`RenderEngine::initialize`)**:
    *   **精确计算总时长**: 引擎预先扫描所有场景，通过读取每个场景中音频文件的实际长度来计算出视频的精确总时长，为准确的进度报告做准备。
    *   **创建输出与编码器**: 初始化 MP4 文件容器、配置 H.264 视频编码器和 AAC 音频编码器。
    *   **准备音频缓冲**: 创建一个 FIFO (First-In, First-Out) 音频缓冲区，这是确保音频数据能以编码器所要求的固定大小被稳定送入的关键。

3.  **循环渲染 (`RenderEngine::render`)**:
    *   引擎按顺序遍历场景列表，依次调用 `renderScene` (场景渲染) 和 `renderTransition` (转场渲染)。
    *   **音视频同步**: 在 `renderScene` 中，通过实时比较视频和音频的时间戳（PTS），来决定下一刻应该编码视频帧还是音频帧，从而实现精确同步。
    *   **视频处理**: 从 `ImageDecoder` 获取图片，交给 `EffectProcessor` 应用 Ken Burns 等特效，最后送入视频编码器。
    *   **音频处理**: 从 `AudioDecoder` 获取解码并重采样后的数据，送入 FIFO 缓冲区，再从缓冲区中取出固定大小的数据块送入音频编码器。
    *   **转场处理**: `renderTransition` 负责处理视频转场效果，并在转场期间向音频流填充静音，以维持同步。
    *   **收尾**: 所有场景渲染完毕后，将缓冲区和编码器中剩余的数据全部“冲洗”并写入文件，完成视频封装。

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
    "name": "勇敢猫咪",
    "output_path": "output/brave_cat.mp4"
  },
  "scenes": [
    {
      "type": "image_scene",
      "duration": 5.0,
      "resources": {
        "image": { "path": "assets/shot1.png" },
        "audio": { "path": "assets/narration1.mp3" }
      },
      "effects": {
        "ken_burns": {
          "enabled": true,
          "preset": "zoom_in"
        }
      }
    },
    {
      "type": "transition",
      "transition_type": "crossfade",
      "duration": 1.0
    },
    {
       "type": "image_scene",
       "duration": 6.0,
       "resources": {
           "image": { "path": "assets/shot2.png" },
           "audio": { "path": "assets/narration2.mp3" }
       },
       "effects": {
        "ken_burns": {
          "enabled": true,
          "preset": "pan_right"
        }
      }
    }
  ]
}
```

### 说明要点：

- **`duration`**:
    - 对于 `image_scene`，如果同时提供了有效的 `audio` 路径，**程序将优先使用音频文件的实际时长**，此时 `duration` 值会被忽略。
    - 如果没有提供音频，或音频文件无法读取，则使用 `duration` 值。
    - 对于 `transition`，`duration` 表示转场的持续时间。

- **`video_scene` 与 `resources.video`**:
    - 在 `video_scene` 中通过 `resources.video.path` 指定视频文件，支持可选的 `trim_start`/`trim_end`（单位秒）以及 `use_audio`。
    - 当 `use_audio` 为 `true` 且未提供 `resources.audio` 时，程序会自动提取视频自带音轨并保持与画面同步。
    - 若同时提供 `resources.audio`，则使用外部音频并可继续使用音量淡入淡出等效果。

- **`effects.ken_burns`**:
    - **`enabled`**: `true` 表示启用特效。
    - **`preset`**: 使用预设的动画效果，方便快速配置。程序当前支持：
        - `"zoom_in"`: 缓慢放大，聚焦画面中心。
        - `"zoom_out"`: 缓慢缩小，从中心向外展示全景。
        - `"pan_right"`: 画面缓慢向右平移。
        - `"pan_left"`: 画面缓慢向左平移。
    - **手动参数**: 你也可以不使用 `preset`，而是手动指定所有六个参数 (`start_scale`, `end_scale`, `start_x`, `start_y`, `end_x`, `end_y`) 来实现更精细的自定义动画。

## 运行与调试

- 将资源放到 `assets/`，编辑 `test_config.json` 指向这些资源。
- 构建完成后，将 `test_config.json` 和 `assets/` 内容复制到可执行文件同目录（CMake 已尝试自动复制）。
- 运行程序并观察控制台日志，若遇到 FFmpeg 相关错误，可查看错误输出并确保 `3rdparty/ffmpeg/bin` 下的 DLL 可用或链接正确的静态库。

## 许可证

MIT License
