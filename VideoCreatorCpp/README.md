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
  /**
   * 项目基本信息配置 (简化版)
   */
  "project": {
    "name": "勇敢猫咪",                         // 项目名称
    "output_path": "自动生成或指定一个临时路径"   // 输出路径。如果未指定文件名，系统可能会根据 name 自动生成
    // 注意：width, height, fps 均未在此处定义，系统将自动应用默认配置：
    // Width: 1920, Height: 1080, FPS: 30, Background: #000000
  },

  /**
   * 场景序列配置
   * 这里的场景没有显式的 "id"，系统将根据数组的顺序自动推断播放流程
   */
  "scenes": [
    /**
     * 第一个场景：图片场景
     */
    {
      "type": "image_scene",                  // 场景类型
      "resources": {
        "image": { 
            "path": "C:/.../shot1.png"        // 图片路径。位置(0,0)、缩放(1.0)、旋转(0) 等参数均使用默认值
        }, 
        "audio": { 
            "path": "C:/.../narration1.mp3"   // 音频路径。音量(1.0)、偏移(0.0) 等参数均使用默认值
        } 
      },
      "effects": {
        "ken_burns": { 
            "enabled": true,                  // 启用推拉摇移特效
            "preset": "zoom_in"               // 使用预设模式："zoom_in" (镜头推进)。
                                              // 系统会自动计算 start_scale/end_scale 和坐标，无需手动指定具体的 x/y 参数
        } 
      }
    },

    /**
     * 转场效果
     * 系统自动识别此转场介于 数组索引[0] 和 数组索引[2] 之间
     */
    {
      "type": "transition",                   // 场景类型：转场
      "transition_type": "crossfade",         // 转场方式：淡入淡出
      "duration": 1.0                         // 转场耗时
      // from_scene 和 to_scene 被省略，系统根据数组顺序自动判定为“上一个场景”到“下一个场景”
    },

    /**
     * 第二个场景：图片场景
     */
    {
       "type": "image_scene",                 // 下一个分镜的内容
       // ... 后续配置逻辑同上 ...
       "resources": {
           // ...
       }
    }
  ]
  // global_effects 被省略，系统将使用默认的编码参数 (H.264/AAC) 和音频标准化设置
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

## 许可证

MIT License
