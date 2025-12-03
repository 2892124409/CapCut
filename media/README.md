# Media - FFmpeg/Qt Quick 多媒体组件

自定义的 `media_core` 库封装了 **视频播放器、音频播放器和图片查看器**，并通过 `Qt Quick` + `FFmpeg` 提供统一的 `MediaController` 渲染入口，可方便地集成到大型 Qt 工程中。

本 README 包含两部分：

1. **使用指南**：如何编译/运行、在现有工程中集成，以及“每个功能对应的示例代码”。
2. **实现详解（面试向）**：整体架构、线程模型、关键算法与常见问题。

---

## 1. 功能概览

- 支持视频格式：`mp4, avi, mkv, mov, wmv, flv, webm, m4v, 3gp, ts` 等
- 支持音频格式：`mp3, wav, flac, aac, ogg, m4a, wma, opus, aiff, ape` 等
- 支持图片格式：`jpg, jpeg, png, bmp, gif, tiff, tif, webp, ico, svg`
- 支持**内存数据源**：可直接播放内存中的完整媒体文件数据（视频/音频/图片），无需落盘临时文件
- 统一控制接口：`MediaController`（QML 类型）统一管理视频、音频和图片
- 可复用 UI：`MediaView` 提供“文件选择 + 进度条 + 音量 + 上一/下一”完整界面
- 文件列表管理：`FileManager` 扫描当前目录并维护简单播放列表
- 键盘快捷键：
  - `Space` 播放/暂停
  - `←/→` 快退/快进 5 秒
  - `↑/↓` 增减音量
  - `M` 静音/取消静音
  - `F` 全屏/退出全屏
  - `Esc` 退出全屏

---

## 2. 环境与构建

### 2.1 依赖

- Qt 6.5+（至少包含 `Core`, `Gui`, `Quick`, `Multimedia`, `OpenGL`）
- CMake 3.16+
- FFmpeg：`avcodec`, `avformat`, `avutil`, `swscale`, `swresample`

克隆工程后，确保 `3rdparty/ffmpeg` 目录存在，并包含：

```text
3rdparty/ffmpeg/
├── include/   # FFmpeg 头文件
└── lib/       # 库文件（当前 CMakeLists 默认链接 *.lib，可按平台调整）
```

如在 Linux/macOS 下使用，请将 `CMakeLists.txt` 中 FFmpeg 链接部分替换为对应的 `.so`/`.dylib` 或包管理器提供的 target。

### 2.2 编译与运行 Demo

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target appmedia
./build/appmedia    # 或在 Qt Creator 中直接运行 appmedia
```

`appmedia` 是演示程序，会加载 `media` QML 模块的 `Main.qml`，展示完整播放器 UI。

### 2.3 目录结构

```text
media/
├── CMakeLists.txt        # 构建脚本，生成 media_core + appmedia
├── main.cpp              # 演示程序入口
├── Main.qml              # Demo 窗口，嵌入 MediaView
├── MediaView.qml         # 可复用的完整播放 UI
├── mediacontroller.*     # QQuickFramebufferObject，统一渲染入口
├── videoplayer_impl.*    # 视频播放实现（Demux + 解码 + 渲染）
├── audioplayer.*, audiodecoder.* # 音频播放链路
├── imageviewer.*         # 图片加载显示
├── demuxer.*, videodecoder.* ... # FFmpeg 解复用/解码封装
└── ffmpeg_resource_manager.h     # FFmpeg RAII 封装
```

---

## 3. 集成到大型工程

### 3.1 CMake 引入方式

假设你的工程目录中包含 `CapCut/media`：

```cmake
add_subdirectory(CapCut/media)         # 引入本目录

target_link_libraries(your_app PRIVATE
    media_core                         # 本库
    Qt6::Quick                         # 视情况添加其他 Qt 模块
)
```

`media_core` 会通过 `qt_add_qml_module` 导出 QML 模块：

- URI：`media`
- 版本：`1.0`
- QML 类型：`MediaView`, `MediaController`, `FileManager`

编译链接完成后，你的 QML 就可以直接：

```qml
import QtQuick
import media 1.0
```

### 3.2 在 QML 中使用（推荐）

#### 3.2.1 一行集成完整播放器：`MediaView`

```qml
import QtQuick
import QtQuick.Controls
import media 1.0

Item {
    width: 800
    height: 600

    MediaView {
        id: view
        anchors.fill: parent

        // 最简单的用法：启动后直接加载一个文件
        Component.onCompleted: view.loadMediaFromPath("/path/to/video.mp4")
    }
}
```

`MediaView` 内部已经包含：

- 顶部文件信息栏（当前文件名、总数量）
- 中间渲染区域（黑色背景 + `MediaController`）
- 底部控制条（进度条、播放/暂停、上一/下一、音量）
- 文件选择对话框 `FileDialog`
- 键盘快捷键（空格、箭头、F/M/Esc 等）

对外暴露了三个属性，方便宿主工程直接访问：

- `property alias controller: mediaController` —— 播放控制核心
- `property alias manager: fileManager` —— 文件/播放列表管理
- `property alias volumeSlider: volumeSlider` —— 音量滑块控件

> 功能示例：访问当前播放文件名  
> ```qml
> Text {
>     text: view.manager.currentFile
> }
> ```

#### 3.2.2 只用渲染控件：`MediaController`

如果你想自己写 UI，仅复用解码和渲染逻辑，可以直接使用 `MediaController`：

```qml
import QtQuick
import QtQuick.Controls
import media 1.0

Item {
    width: 800
    height: 600

    MediaController {
        id: controller
        anchors.fill: parent
        Component.onCompleted: controller.loadMedia("/path/to/demo.mp4")
    }

    Row {
        anchors.bottom: parent.bottom
        spacing: 10

        Button { text: "Play";  onClicked: controller.play() }
        Button { text: "Pause"; onClicked: controller.pause() }
        Button { text: "Stop";  onClicked: controller.stop() }
    }

    Slider {
        id: progress
        anchors.bottom: parent.bottom
        width: parent.width
        from: 0
        to: controller.duration
        value: controller.position
        onMoved: controller.seek(value)         // 拖动 seek
    }

    Slider {
        id: volume
        anchors.bottom: progress.top
        width: parent.width
        from: 0.0
        to: 1.0
        value: 1.0
        onValueChanged: controller.setVolume(value)  // 音量控制
    }
}
```

> 上面这段代码覆盖了 `MediaController` 的所有核心功能：  
> - `loadMedia(path)`：加载视频/音频/图片  
> - `play() / pause() / stop()`：播放控制  
> - `seek(ms)`：拖动进度条  
> - `setVolume(volume)`：0.0~1.0 音量设置  
> - 属性 `duration/position/playing/paused/stopped` 用于绑定 UI

#### 3.2.3 使用 `FileManager` 实现上一/下一与简单播放列表

```qml
import QtQuick
import QtQuick.Controls
import media 1.0

Item {
    FileManager { id: manager }
    MediaController { id: controller; anchors.fill: parent }

    Component.onCompleted: {
        // 以指定文件为起点，自动扫描同目录下所有媒体
        manager.scanFolderForFile("D:/Videos/demo.mp4")
        var first = manager.currentFile
        if (first)
            controller.loadMedia(first)
    }

    Row {
        anchors.bottom: parent.bottom
        spacing: 10

        Button {
            text: "Prev"
            enabled: manager.hasPrevious
            onClicked: {
                var file = manager.getPreviousFile()
                if (file) controller.loadMedia(file)
            }
        }

        Button {
            text: "Next"
            enabled: manager.hasNext
            onClicked: {
                var file = manager.getNextFile()
                if (file) controller.loadMedia(file)
            }
        }
    }
}
```

> 上面的代码示例涵盖了 `FileManager` 的所有核心功能：  
> - `scanFolder(folderPath)`：扫描某个目录  
> - `scanFolderForFile(filePath)`：以某个文件为起点扫描同目录  
> - `getNextFile()/getPreviousFile()`：上一/下一媒体文件  
> - `getFileByIndex(index)`：按索引取文件  
> - 属性 `currentFile/currentFolder/videoFiles/currentIndex/hasPrevious/hasNext`

### 3.3 从 C++ 侧驱动/控制

如果你有自己的 QML 主界面，只想在其中嵌入 `MediaView` 或 `MediaController` 并从 C++ 侧控制：

#### 3.3.1 用 `QQmlApplicationEngine` 加载自己的 QML

```cpp
// main.cpp（宿主工程）
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // 确保使用 OpenGL 渲染
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QQmlApplicationEngine engine;
    const QUrl url(u"qrc:/main.qml"_qs);   // 你的主 QML
    engine.load(url);
    return app.exec();
}
```

在 `main.qml` 中：

```qml
import QtQuick
import media 1.0

Item {
    width: 800
    height: 600

    MediaController {
        id: mediaController
        objectName: "mediaController"   // 方便 C++ 查找
        anchors.fill: parent
    }
}
```

#### 3.3.2 从 C++ 获取 `MediaController` 并调用方法

```cpp
auto rootObjects = engine.rootObjects();
if (rootObjects.isEmpty())
    return;

QObject *root = rootObjects.first();
QObject *controller = root->findChild<QObject *>("mediaController");
if (!controller)
    return;

// 示例：加载并播放一个文件
QMetaObject::invokeMethod(controller, "loadMedia",
                          Q_ARG(QVariant, QVariant::fromValue(QString("D:/Videos/demo.mp4"))));
QMetaObject::invokeMethod(controller, "play");

// 连接错误信号
QObject::connect(controller, SIGNAL(errorOccurred(QString)),
                 &app, [] (const QString &err) {
                     qWarning() << "Media error:" << err;
                 });
```

> 上述代码覆盖了 C++ 侧使用 `MediaController` 的典型场景：加载、播放、错误处理。  
> 需要更多控制（如暂停、seek、改变音量）时，只需继续调用对应的 Q_INVOKABLE 方法。

#### 3.3.3 从内存加载媒体（不落盘）

在某些场景下，你拿到的是**内存中的完整媒体文件数据**（例如：从网络/加密容器中解密后的 `QByteArray`），不希望先写入磁盘再播放。  
为此 `MediaController` 提供了 3 个专门的接口：

- `bool loadVideoFromMemory(const QByteArray &data, const QString &formatHint = QString())`
- `bool loadAudioFromMemory(const QByteArray &data, const QString &formatHint = QString())`
- `bool loadImageFromMemory(const QByteArray &data, const QString &formatHint = QString())`

内部通过 FFmpeg 的自定义 `AVIOContext` 在内存上解复用数据，后续解码与渲染链路与文件方式完全一致，支持 `seek`、音画同步等。

**C++ 示例：从内存播放视频**

```cpp
QByteArray mediaData = ...;  // 这里填入完整的视频文件数据（如 .mp4）

QObject *root = engine.rootObjects().first();
QObject *controller = root->findChild<QObject *>("mediaController");
if (!controller)
    return;

bool ok = false;
QMetaObject::invokeMethod(
    controller, "loadVideoFromMemory",
    Q_RETURN_ARG(bool, ok),
    Q_ARG(QByteArray, mediaData),
    Q_ARG(QString, QString())      // formatHint 可留空（视频通常不需要）
);
if (ok) {
    QMetaObject::invokeMethod(controller, "play");
}
```

**C++ 示例：从内存播放图片**

```cpp
QByteArray imageData = ...;  // 完整图片数据

bool ok = false;
QMetaObject::invokeMethod(
    controller, "loadImageFromMemory",
    Q_RETURN_ARG(bool, ok),
    Q_ARG(QByteArray, imageData),
    Q_ARG(QString, QStringLiteral("png"))  // 可选：显式提示格式
);
```

> 提示：  
> - 这里的 `data` 必须是**完整的封装文件数据**（例如完整 MP4 文件、完整 PNG 文件），而不是裸 H264/NV12 帧或原始 PCM。  
> - 对视频/音频数据，`formatHint` 通常可以留空，FFmpeg 会自己探测封装格式；对某些图片（特别是不带后缀时），建议显式传入 `"png"`、`"jpeg"` 等格式名。

#### 3.3.4 在 QML 中触发内存播放（桥接对象示例）

通常“内存中的媒体数据”是由 C++ 层获取/生成的（例如网络下载、解密、拼接而成的 `QByteArray`）。推荐的做法是：

1. C++ 层通过一个“桥接对象”（如 `MediaBridge`）持有这些内存数据。
2. 把桥接对象暴露给 QML（`setContextProperty` 或 `qmlRegisterType`）。
3. 在 QML 中，把 `MediaController` 的对象引用传给桥接对象，由 C++ 去调用 `loadVideoFromMemory` 等接口。

**步骤一：定义桥接对象（示例代码）**

```cpp
// mediabridge.h
#include <QObject>
#include <QByteArray>

class MediaBridge : public QObject
{
    Q_OBJECT
public:
    explicit MediaBridge(QObject *parent = nullptr) : QObject(parent) {}

    // 假设你在其他地方把数据写入 m_videoData
    void setVideoData(const QByteArray &data) { m_videoData = data; }
    void setImageData(const QByteArray &data) { m_imageData = data; }

    Q_INVOKABLE void playVideoFromMemory(QObject *mediaController);
    Q_INVOKABLE void showImageFromMemory(QObject *mediaController);

private:
    QByteArray m_videoData;
    QByteArray m_imageData;
};

// mediabridge.cpp
#include "mediabridge.h"
#include <QMetaObject>
#include <QDebug>

void MediaBridge::playVideoFromMemory(QObject *mediaController)
{
    if (!mediaController) {
        qWarning() << "MediaBridge: mediaController is null";
        return;
    }
    if (m_videoData.isEmpty()) {
        qWarning() << "MediaBridge: no video data available";
        return;
    }

    bool ok = false;
    QMetaObject::invokeMethod(
        mediaController, "loadVideoFromMemory",
        Q_RETURN_ARG(bool, ok),
        Q_ARG(QByteArray, m_videoData),
        Q_ARG(QString, QString())  // 视频通常不需要 formatHint
    );
    if (ok) {
        QMetaObject::invokeMethod(mediaController, "play");
    } else {
        qWarning() << "MediaBridge: loadVideoFromMemory failed";
    }
}

void MediaBridge::showImageFromMemory(QObject *mediaController)
{
    if (!mediaController || m_imageData.isEmpty())
        return;

    bool ok = false;
    QMetaObject::invokeMethod(
        mediaController, "loadImageFromMemory",
        Q_RETURN_ARG(bool, ok),
        Q_ARG(QByteArray, m_imageData),
        Q_ARG(QString, QStringLiteral("png"))  // 若能确定格式，建议传入
    );
}
```

**步骤二：把桥接对象注入到 QML 上下文**

```cpp
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "mediabridge.h"

QQmlApplicationEngine engine;

MediaBridge bridge;
// 例如，从网络或文件读取数据到内存
QByteArray fileData = ...;   // 视频数据
QByteArray imageData = ...;  // 图片数据
bridge.setVideoData(fileData);
bridge.setImageData(imageData);

engine.rootContext()->setContextProperty("mediaBridge", &bridge);
engine.load(u"qrc:/main.qml"_qs);
```

**步骤三：在 QML 中调用桥接对象，驱动内存播放**

```qml
import QtQuick
import QtQuick.Controls
import media 1.0

Item {
    width: 800
    height: 600

    // 仍然使用 MediaController 作为渲染/控制核心
    MediaController {
        id: mediaController
        objectName: "mediaController"
        anchors.fill: parent
    }

    Column {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 10

        Button {
            text: "播放内存中的视频"
            onClicked: mediaBridge.playVideoFromMemory(mediaController)
        }

        Button {
            text: "显示内存中的图片"
            onClicked: mediaBridge.showImageFromMemory(mediaController)
        }
    }
}
```

通过这种方式，QML 侧完全不需要接触二进制数据，只负责把 `MediaController` 的对象引用传回给 C++，由 C++ 负责：

- 获取/维护内存中的媒体数据（网络、磁盘、解密等来源）。
- 调用 `MediaController::loadVideoFromMemory` / `loadImageFromMemory` 等函数。
- 在必要时决定何时重新加载或切换媒体内容。

---

## 4. 功能一览与示例代码

这一节逐个列出对外可用的“功能/接口”，并附上简短示例。

### 4.1 `MediaController`（统一媒体控制）

QML 类型声明（见 `mediacontroller.h`）：

- 属性：
  - `qint64 duration`：当前媒体总时长（毫秒）
  - `qint64 position`：当前播放位置（毫秒）
  - `bool playing`：是否正在播放
  - `bool paused`：是否处于暂停状态
  - `bool stopped`：是否处于停止状态
- 方法（Q_INVOKABLE）：
  - `bool loadMedia(const QString &filePath)`
  - `bool loadVideoFromMemory(const QByteArray &data, const QString &formatHint = QString())`
  - `bool loadAudioFromMemory(const QByteArray &data, const QString &formatHint = QString())`
  - `bool loadImageFromMemory(const QByteArray &data, const QString &formatHint = QString())`
  - `void play()`
  - `void pause()`
  - `void stop()`
  - `void seek(qint64 position)`
  - `void setVolume(float volume)`（0.0 ~ 1.0）
- 信号：
  - `durationChanged()`, `positionChanged()`
  - `playingStateChanged()`, `pausedStateChanged()`, `stoppedStateChanged()`
  - `errorOccurred(const QString &error)`

**示例：所有功能的典型使用**

```qml
MediaController {
    id: controller
    anchors.fill: parent

    Component.onCompleted: {
        // 1. 加载媒体（支持视频/音频/图片）
        if (controller.loadMedia("/path/to/media.mp4")) {
            controller.play()           // 2. 开始播放
        }
    }

    onErrorOccurred: (err) => console.log("Media error:", err)
}

// 3. 播放/暂停/停止
Button { text: "Play";  onClicked: controller.play() }
Button { text: "Pause"; onClicked: controller.pause() }
Button { text: "Stop";  onClicked: controller.stop() }

// 4. 拖动进度条 seek
Slider {
    from: 0
    to: controller.duration
    value: controller.position
    onMoved: controller.seek(value)
}

// 5. 调整音量
Slider {
    from: 0.0; to: 1.0; value: 1.0
    onValueChanged: controller.setVolume(value)
}

// 6. 绑定播放状态
Text {
    text: controller.playing ? "Playing" :
          controller.paused  ? "Paused"  : "Stopped"
}
```

### 4.2 `MediaView`（完整播放器 UI）

对外功能：

- `loadMediaFromPath(filePath: string)`：从绝对路径加载媒体并自动更新播放列表
- `controller`：内部 `MediaController` 的别名
- `manager`：内部 `FileManager` 的别名
- `volumeSlider`：内部音量控件的别名
- 内建键盘快捷键（空格、方向键、F/m/Esc）

**示例：使用内置 UI 同时访问内部对象**

```qml
MediaView {
    id: view
    anchors.fill: parent

    Component.onCompleted: {
        view.loadMediaFromPath("D:/Videos/demo.mp4")
    }

    // 访问内部 MediaController
    MouseArea {
        anchors.fill: parent
        onDoubleClicked: {
            if (view.controller.paused)
                view.controller.play()
            else
                view.controller.pause()
        }
    }

    // 访问内部 FileManager
    Text {
        anchors.top: parent.top
        text: view.manager.currentFile
    }
}
```

### 4.3 `FileManager`（文件/播放列表管理）

QML 类型声明（见 `filemanager.h`）：

- 属性：
  - `QString currentFile`
  - `QString currentFolder`
  - `QStringList videoFiles`
  - `int currentIndex`
  - `bool hasPrevious`
  - `bool hasNext`
- 方法（Q_INVOKABLE）：
  - `void scanFolder(const QString &folderPath)`
  - `void scanFolderForFile(const QString &filePath)`
  - `QString getNextFile()`
  - `QString getPreviousFile()`
  - `QString getFileByIndex(int index)`

**示例：按索引选文件 + 上一/下一**

```qml
FileManager { id: manager }
MediaController { id: controller }

Component.onCompleted: {
    manager.scanFolder("D:/Videos")
    var first = manager.getFileByIndex(0)   // 使用 getFileByIndex
    if (first) controller.loadMedia(first)
}

Button {
    text: "上一首"
    enabled: manager.hasPrevious
    onClicked: {
        var prev = manager.getPreviousFile()
        if (prev) controller.loadMedia(prev)
    }
}

Button {
    text: "下一首"
    enabled: manager.hasNext
    onClicked: {
        var next = manager.getNextFile()
        if (next) controller.loadMedia(next)
    }
}
```

### 4.4 键盘快捷键（MediaView 内置）

`MediaView.qml` 中已经处理了以下按键（通过 `Keys.onPressed`）：

- `Space`：在播放和暂停之间切换
- `←`：向后 seek 5 秒
- `→`：向前 seek 5 秒
- `↑`：音量 +0.1
- `↓`：音量 -0.1
- `M`：静音/恢复音量
- `F`：全屏/退出全屏
- `Esc`：从全屏恢复到普通窗口

如果你自己写 UI，可以直接参考 `MediaView.qml` 里这段键盘处理逻辑拷贝使用。

---

## 5. 实现详解（面试向）

这一部分更偏底层原理，如果你只是使用组件，可以跳过；如果你想在面试中详细讲解播放器实现，可以重点阅读。

### 5.1 整体架构与渲染流程

1. **MediaView.qml**  
   - 提供 UI：文件信息栏、控制按钮、进度条与音量调节。  
   - 暴露 `controller`/`manager` 的 alias，方便宿主工程绑定。  
   - 提供 `loadMediaFromPath()` 统一加载入口。

2. **MediaController (QQuickFramebufferObject)**  
   - `loadMedia()` 根据文件后缀创建 `VideoPlayerImpl`、`AudioPlayer` 或 `ImageViewer`。  
   - 通过 `frameChanged` 信号获取最新 QImage，写入受 `QReadWriteLock` 保护的缓冲区。  
   - `MediaRenderer` 在渲染线程中取帧，上传为 OpenGL 纹理，按照 FBO 尺寸缩放居中绘制。  
   - 对 QML 暴露统一属性：`duration/position/playing/paused/stopped`，供进度条与按钮绑定。

3. **示例运行流程（视频）**  
   ```text
   Main.qml -> MediaView -> MediaController.loadMedia()
     -> VideoPlayerImpl::load()
         -> Demuxer::open()  (读取文件/网络流)
         -> 创建 VideoDecoder 线程，AudioDecoder 线程
     -> 播放时 MediaController::play() -> VideoPlayerImpl::play()
         -> 解复用线程不断向音/视频队列投喂 AVPacket
         -> 视频线程解码为 QImage，AudioDecoder 输出 PCM 到 QAudioSink
         -> MediaController 接收 frameChanged()，触发 update() 进入渲染
   ```

### 5.2 视频播放链路

- **Demuxer**：独立线程读取 FFmpeg 的 `AVFormatContext`。`popVideoPacket()`/`popAudioPacket()` 供解码器消费。
- **VideoDecoder (QThread)**：
  - 调用 `avcodec_send_packet` / `avcodec_receive_frame` 解码。
  - 使用 `sws_scale` 将 `AVFrame` 转成 `QImage`（RGBA）。  
  - 将 `VideoFrame` (QImage + pts) 放入环形队列，最大 10 帧，避免内存暴涨。  
  - 支持 `requestPause/requestStop/requestFlush`，`setDropUntil()` 用于 seek 后丢弃旧帧。  

- **VideoPlayerImpl**：
  - 拥有 `QTimer` 驱动，按音频时钟同步视频。  
  - `onTimerFire()` 读取最新帧，对齐 `pts` 与音频播放进度，发射 `frameChanged`。  
  - 维护播放状态、缓存 `duration` 和 `position`，并通过接口报告给 `MediaController`。

### 5.3 音频播放链路

- **AudioDecoder**：
  - 解析音频流，转换为 `PCM`（通常 `AV_SAMPLE_FMT_S16` 或 `F32`）。  
  - 通过 `QAudioSink` 推送到声卡，利用其缓冲区进行流控。  
  - `seek` 时调用 `requestFlush()` + 重新创建 `QAudioSink` 清空残留音频，避免拖动后播放旧数据（详见 `Note.md`）。

### 5.4 图片查看器

- `ImageViewer` 直接使用 `QImage` 加载文件，发射一次 `frameChanged`。  
- `MediaController` 接收到帧后立即渲染，没有额外线程。  
- 为防止旧播放器信号影响图片显示，在各个槽函数中使用 `sender() == m_currentPlayer` 做保护（详见 `Note.md` 中“图片切换黑屏问题”）。

### 5.5 控制条与文件管理

- `FileManager`：扫描当前目录下的媒体文件，维护 `videoFiles`、`currentIndex`。提供 `getNextFile/getPreviousFile/getFileByIndex` 接口，供按钮调用。  
- 进度条绑定 `mediaController.position/duration`，拖动时暂停播放、更新目标位置、放开后 seek 并恢复播放。  
- 上/下一个按钮依赖 `hasPrevious/hasNext` 属性，通过 `scanFolderForFile()` 在打开单个文件时自动加载同目录下的所有媒体，实现简单的播放列表。

### 5.6 多线程与同步

- **Demuxer 线程**：负责读取磁盘/网络流，往 `QQueue<AVPacket*>` 推数据。  
- **VideoDecoder 线程**：解码视频、SWS 转换、写入帧队列。  
- **AudioDecoder 线程**：解码音频并喂给 `QAudioSink`。  
- **主 GUI 线程**：执行 `MediaController::onFrameChanged()`、更新状态、驱动 QML UI。  
- 通过以下机制保证线程安全：
  - `QReadWriteLock m_frameLock`：防止渲染线程与 UI 线程同时访问 `m_currentFrame`。  
  - `std::atomic` 缓存时长/播放状态，避免频繁加锁。  
  - `QWaitCondition` + `QMutex` 控制帧队列大小，当解码过快时等待消费。  
  - Seek 时使用 `requestFlush`/`clearFrameQueue` 清空旧数据，配合 `dropUntil` 避免旧帧造成画面跳动。

### 5.7 常见问题&解答

| 主题 | 核心思路 |
|------|---------|
| 为什么用 `QQuickFramebufferObject`? | 直接掌控 OpenGL 渲染，避免 `QVideoSink` 的格式/性能限制；还能统一视频、图片渲染。 |
| 如何解决 seek 时画面/音频滞后? | 解码器在 `setDropUntil()` 后丢弃目标时间之前的帧，同时刷新解码器缓冲；音频端重新创建 `QAudioSink`。详见 `Note.md`。 |
| 多平台 DPI 适配? | `MediaRenderer::synchronize()` 使用 `window()->effectiveDevicePixelRatio()` 计算 FBO 大小，确保视频填满视图。 |
| 线程退出顺序? | `VideoDecoder`/`AudioDecoder` 在析构或 `stop()` 中调用 `requestStop()` + `wait()`，确保线程安全退出后再释放 `AVCodecContext` 等资源。 |
| 如何扩展数据来源/格式? | 可在 `MediaController::createMediaPlayer()` 中增加后缀判断；若需要自定义数据源，可包装 `AVIOContext` 并在 `Demuxer` 中注入。 |

---

## 6. 小结

- `media_core` 提供了一套完整的多媒体播放能力：统一的 `MediaController` 渲染面板 + 可复用的 `MediaView` UI 组件 + `FileManager` 播放列表。  
- 通过 `qt_add_qml_module` 输出 `media 1.0`，宿主项目可直接 `import media 1.0` 复用。  
- 架构上采用 Demux/Decoder/Renderer 分层，配合多线程和锁机制保障实时播放与平滑拖动。  
- 如果你需要在面试中展示多媒体方向的实践经验，可以以 README + Note.md 为提纲，重点讲解 seek 处理、多线程同步和 OpenGL 渲染细节。

欢迎根据业务需要继续扩展，比如：字幕支持、自定义滤镜、完整播放列表系统、网络流协议等。只需在 `media_core` 内部增量开发，宿主工程即可复用新能力。祝集成顺利!
