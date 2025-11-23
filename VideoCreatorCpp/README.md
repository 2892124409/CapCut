# VideoCreatorCpp - 基于FFmpeg的视频合成器

一个基于FFmpeg的C++视频合成器，支持图片、视频、音频的合成和特效处理。

## 项目架构

```
VideoCreatorCpp/
├── CMakeLists.txt              # CMake 构建脚本
├── config.json                 # 项目配置文件
├── assets/                     # 资源文件目录
├── src/
│   ├── main.cpp                # 程序入口
│   ├── common/                 # 通用工具
│   ├── model/                  # 数据层
│   ├── ffmpeg_utils/           # FFmpeg封装层
│   ├── filter/                 # 滤镜图构建层
│   └── engine/                 # 业务逻辑层
└── third_party/                # 第三方库
```

## 功能特性

- ✅ 支持图片、视频、音频资源合成
- ✅ 支持淡入淡出、缩放、平移等特效
- ✅ 基于JSON配置驱动
- ✅ RAII资源管理
- ✅ 模块化设计

## 构建说明

### 前置要求

- CMake 3.16+
- FFmpeg 库 (已包含在项目中的3rdparty目录)
- C++17 编译器

### 构建步骤

```bash
# 创建构建目录
mkdir build
cd build

# 配置项目
cmake ..

# 编译
cmake --build .

# 运行
./VideoCreatorCpp
```

## 配置说明

编辑 `config.json` 文件来配置您的视频项目：

```json
{
  "project_name": "项目名称",
  "output": {
    "output_path": "输出文件路径",
    "width": 1920,
    "height": 1080,
    "frame_rate": 30,
    "video_bitrate": 4000000,
    "audio_bitrate": 128000,
    "video_codec": "libx264",
    "audio_codec": "aac"
  },
  "scenes": [
    {
      "name": "场景名称",
      "duration": 5.0,
      "resources": [
        {
          "path": "资源文件路径",
          "type": "IMAGE|VIDEO|AUDIO",
          "start_time": 0.0,
          "duration": 5.0,
          "volume": 1.0
        }
      ],
      "effects": [
        {
          "type": "FADE_IN|FADE_OUT|ZOOM|PAN|ROTATE",
          "start_time": 0.0,
          "duration": 1.0,
          "params": "特效参数"
        }
      ]
    }
  ]
}
```

## 资源准备

将您的资源文件放在 `assets/` 目录下：

- 图片：JPG、PNG格式
- 视频：MP4、MOV等格式
- 音频：MP3、WAV等格式

## 使用示例

1. 将您的资源文件放入 `assets/` 目录
2. 编辑 `config.json` 配置项目
3. 构建并运行程序
4. 在 `output/` 目录查看生成的视频

## 技术架构

- **数据层 (model/)**: 项目配置和数据结构定义
- **FFmpeg封装层 (ffmpeg_utils/)**: FFmpeg资源的RAII封装
- **滤镜层 (filter/)**: FFmpeg滤镜图构建和管理
- **业务逻辑层 (engine/)**: 解码、编码、渲染引擎
- **通用工具 (common/)**: 日志、工具函数

## 许可证

MIT License
