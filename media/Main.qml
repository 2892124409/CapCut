import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import media // 你的 C++ 模块名

Window {
    width: 1000
    height: 700
    visible: true
    title: qsTr("剪映实习生 - 全功能播放器 (C++ FFmpeg OpenGL)")
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint

    // 键盘焦点容器 - 强制焦点管理
    Item {
        anchors.fill: parent
        focus: true
        
        // 确保焦点始终在此Item上
        onFocusChanged: {
            if (!focus) {
                focus = true
            }
        }
        
        // 鼠标点击时重新获取焦点
        MouseArea {
            anchors.fill: parent
            onClicked: {
                parent.focus = true
            }
        }
        
        // 键盘控制
        Keys.onPressed: (event) => {
            switch(event.key) {
                case Qt.Key_Space:
                    if (mediaController.paused) mediaController.pause()
                    else mediaController.play()
                    event.accepted = true
                    break
                case Qt.Key_Left:
                    if (mediaController.duration > 0) {
                        var seekBack = Math.max(0, mediaController.position - 5000) // 后退5秒
                        mediaController.seek(seekBack)
                    }
                    event.accepted = true
                    break
                case Qt.Key_Right:
                    if (mediaController.duration > 0) {
                        var seekForward = Math.min(mediaController.duration, mediaController.position + 5000) // 前进5秒
                        mediaController.seek(seekForward)
                    }
                    event.accepted = true
                    break
                case Qt.Key_Up:
                    volumeSlider.value = Math.min(1.0, volumeSlider.value + 0.1)
                    event.accepted = true
                    break
                case Qt.Key_Down:
                    volumeSlider.value = Math.max(0.0, volumeSlider.value - 0.1)
                    event.accepted = true
                    break
                case Qt.Key_M:
                    volumeSlider.value = volumeSlider.value > 0 ? 0 : 1.0 // 静音切换
                    event.accepted = true
                    break
                case Qt.Key_F:
                    if (visibility === Window.FullScreen) {
                        showNormal()
                    } else {
                        showFullScreen()
                    }
                    event.accepted = true
                    break
                case Qt.Key_Escape:
                    if (visibility === Window.FullScreen) {
                        showNormal()
                    }
                    event.accepted = true
                    break
            }
        }
    }

    // 黑色背景容器 (防止视频未加载时白屏)
    Rectangle {
        anchors {
            top: fileInfoBar.bottom
            bottom: controls.top
            left: parent.left
            right: parent.right
        }
        color: "black"

        // === 1. 播放器核心组件 ===
        MediaController {
            id: mediaController
            anchors.fill: parent
        }
    }

    // 文件管理器（通过C++注册）
    FileManager {
        id: fileManager
    }

    // 文件信息显示（保留在顶部）
    Rectangle {
        id: fileInfoBar
        height: 30
        color: "#2a2a2a"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right

        Row {
            anchors.centerIn: parent
            spacing: 20

            Text {
                id: fileNameText
                text: fileManager.currentFile ? fileManager.currentFile.split("/").pop() : "未选择文件"
                color: "#e0e0e0"
                font.pixelSize: 14
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: fileTypeText
                text: fileManager.currentFileType === "video" ? "🎥 视频" : 
                      fileManager.currentFileType === "image" ? "🖼️ 图片" : 
                      fileManager.currentFileType === "audio" ? "🎵 音频" : ""
                color: "#aaa"
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: fileCountText
                text: fileManager.mediaFiles.length > 0 ? 
                      "文件 " + (fileManager.currentIndex + 1) + "/" + fileManager.mediaFiles.length : ""
                color: "#aaa"
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // 文件选择对话框
    FileDialog {
        id: fileDialog
        title: "请选择媒体文件"
        nameFilters: [
            // 1. 【新增】将这个放在第一位，作为默认选项
            "媒体文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.3gp *.ts *.jpg *.jpeg *.png *.bmp *.gif *.tiff *.tif *.webp *.ico *.svg *.mp3 *.wav *.flac *.aac *.ogg *.m4a *.wma *.opus *.aiff *.ape)",
            
            // 2. 原有的分类选项
            "视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.3gp *.ts)",
            "图片文件 (*.jpg *.jpeg *.png *.bmp *.gif *.tiff *.tif *.webp *.ico *.svg)",
            "音频文件 (*.mp3 *.wav *.flac *.aac *.ogg *.m4a *.wma *.opus *.aiff *.ape)",
            "所有文件 (*)"
        ]
        onAccepted: {
            var filePath = fileDialog.selectedFile.toString().replace("file:///", "")
            // 扫描文件夹并设置当前文件
            fileManager.scanFolderForMedia(filePath)
            
            // 使用统一的加载方法
            mediaController.loadMedia(filePath)
        }
    }

    // 底部控制条
    Rectangle {
        id: controls



        height: mediaController.mediaType === "image" ? 60 : 90
        color: "#1a1a1a"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        Column {
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                bottom: parent.bottom
                leftMargin: 20
                rightMargin: 20
                topMargin: 10
                bottomMargin: 10
            }
            spacing: 8

            // 进度条容器
            Item {
                id: progressBarContainer
                width: parent.width
                height: 30

                // 【修改点 2】如果是图片模式，直接隐藏进度条
                visible: mediaController.mediaType !== "image"

                // 进度条背景
                Rectangle {
                    id: progressBackground
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    height: 6
                    radius: 3
                    color: "#555"

                    // 已播放进度
                    Rectangle {
                        id: progressPlayed
                        anchors {
                            left: parent.left
                            top: parent.top
                            bottom: parent.bottom
                        }
                        width: mediaController.duration > 0 ? (mediaController.position / mediaController.duration) * parent.width : 0
                        radius: 3
                        color: "#00CCFF"
                    }

                    // 缓冲进度（模拟）
                    Rectangle {
                        id: progressBuffered
                        anchors {
                            left: parent.left
                            top: parent.top
                            bottom: parent.bottom
                        }
                        width: mediaController.duration > 0 ? Math.min((mediaController.position + 5000) / mediaController.duration, 1) * parent.width : 0
                        radius: 3
                        color: "#666"
                        opacity: 0.6
                    }


                    // 滑块
                    Rectangle {
                        id: progressHandle
                        x: mediaController.duration > 0 ? (mediaController.position / mediaController.duration) * progressBackground.width - width/2 : 0
                        y: progressBackground.height / 2 - height / 2
                        width: progressHandleArea.containsMouse || progressHandleArea.pressed ? 16 : 12
                        height: progressHandleArea.containsMouse || progressHandleArea.pressed ? 16 : 12
                        radius: width / 2
                        color: progressHandleArea.pressed ? "#f0f0f0" : "#ffffff"
                        border.color: "#ccc"
                        border.width: 1

                        Behavior on width { NumberAnimation { duration: 100 } }
                        Behavior on height { NumberAnimation { duration: 100 } }
                    }

                    // 滑块交互区域 - 只响应滑块拖动
                    MouseArea {
                        id: progressHandleArea
                        anchors.fill: parent
                        hoverEnabled: true
                        drag.target: progressHandle
                        drag.axis: Drag.XAxis
                        drag.minimumX: 0
                        drag.maximumX: progressBackground.width

                        property bool wasPlayingBeforeDrag: false

                        // 只在拖动滑块时暂停播放
                        onPressed: (mouse) => {
                            wasPlayingBeforeDrag = !mediaController.paused
                            mediaController.pause()
                        }

                        onReleased: {
                            if (mediaController.duration > 0) {
                                var normalizedPos = Math.max(0, Math.min(1, progressHandle.x / progressBackground.width))
                                var seekPos = normalizedPos * mediaController.duration
                                mediaController.seek(seekPos)
                                
                                if (wasPlayingBeforeDrag) {
                                    mediaController.play()
                                }
                            }
                        }
                    }
                }

                // 当前时间显示
                Text {
                    id: currentTimeText
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: 5
                    }
                    color: "white"
                    font.pixelSize: 12
                    text: formatTime(mediaController.position)

                    function formatTime(ms) {
                        var seconds = Math.floor(ms / 1000)
                        var minutes = Math.floor(seconds / 60)
                        var hours = Math.floor(minutes / 60)
                        
                        seconds = seconds % 60
                        minutes = minutes % 60
                        
                        if (hours > 0) {
                            return hours.toString().padStart(2, '0') + ":" +
                                   minutes.toString().padStart(2, '0') + ":" +
                                   seconds.toString().padStart(2, '0')
                        } else {
                            return minutes.toString().padStart(2, '0') + ":" +
                                   seconds.toString().padStart(2, '0')
                        }
                    }
                }

                // 总时长显示
                Text {
                    id: totalTimeText
                    anchors {
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        rightMargin: 5
                    }
                    color: "white"
                    font.pixelSize: 12
                    text: formatTime(mediaController.duration)

                    function formatTime(ms) {
                        var seconds = Math.floor(ms / 1000)
                        var minutes = Math.floor(seconds / 60)
                        var hours = Math.floor(minutes / 60)
                        
                        seconds = seconds % 60
                        minutes = minutes % 60
                        
                        if (hours > 0) {
                            return hours.toString().padStart(2, '0') + ":" +
                                   minutes.toString().padStart(2, '0') + ":" +
                                   seconds.toString().padStart(2, '0')
                        } else {
                            return minutes.toString().padStart(2, '0') + ":" +
                                   seconds.toString().padStart(2, '0')
                        }
                    }
                }
            }

            // --- 控制按钮层：选择文件 | 上一个 播放/暂停 下一个 | 音量 ---
            Row {
                width: parent.width
                height: 40
                spacing: 15

                // 1. 选择媒体文件按钮 (最左边)
                Button {
                    text: "📁 选择媒体文件"
                    font.bold: true
                    font.pixelSize: 14
                    width: 140
                    background: Rectangle {
                        color: parent.down ? "#444" : "#555";
                        radius: 6;
                        border.color: "#666"
                    }
                    contentItem: Text {
                        text: parent.text;
                        font: parent.font;
                        color: "white";
                        horizontalAlignment: Text.AlignHCenter;
                        verticalAlignment: Text.AlignVCenter
                    }
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        fileDialog.open()
                    }
                }

                Item {
                    width: parent.width - 140 - 150 - 20 // 动态计算中间空间
                    height: parent.height

                    // 2. 上一个按钮 (在播放/暂停左侧)
                    Button {
                        id: prevButton
                        anchors.right: playPauseButton.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⏮ 上一个"
                        font.bold: true
                        font.pixelSize: 14
                        width: 100
                        enabled: fileManager.hasPrevious
                        background: Rectangle {
                            color: parent.down ? "#444" : (enabled ? "#555" : "#333");
                            radius: 6;
                            border.color: enabled ? "#666" : "#444"
                        }
                        contentItem: Text {
                            text: parent.text;
                            font: parent.font;
                            color: enabled ? "white" : "#888";
                            horizontalAlignment: Text.AlignHCenter;
                            verticalAlignment: Text.AlignVCenter
                        }
                        focusPolicy: Qt.NoFocus
                        // ... inside prevButton ...
onClicked: {
    var prevFile = fileManager.getPreviousFile()
    if (prevFile) {
        // 使用统一的加载方法
        mediaController.loadMedia(prevFile)
    }
}
                    }

                    // 3. 播放/暂停按钮 (居中)
                    Button {
                        id: playPauseButton
                        anchors.centerIn: parent
                        width: 140; height: 40
                        text: mediaController.paused ? "▶ 播 放" : "⏸ 暂 停"
                        font.bold: true; font.pixelSize: 16
                        background: Rectangle { color: parent.down ? "#333" : "#444"; radius: 8; border.color: "#555" }
                        contentItem: Text { text: parent.text; font: parent.font; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        focusPolicy: Qt.NoFocus // 防止按钮窃取焦点
                        onClicked: {
                            if (mediaController.paused) mediaController.play();
                            else mediaController.pause();
                        }
                    }

                    // 4. 下一个按钮 (在播放/暂停右侧)
                    Button {
                        id: nextButton
                        anchors.left: playPauseButton.right
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⏭ 下一个"
                        font.bold: true
                        font.pixelSize: 14
                        width: 100
                        enabled: fileManager.hasNext
                        background: Rectangle {
                            color: parent.down ? "#444" : (enabled ? "#555" : "#333");
                            radius: 6;
                            border.color: enabled ? "#666" : "#444"
                        }
                        contentItem: Text {
                            text: parent.text;
                            font: parent.font;
                            color: enabled ? "white" : "#888";
                            horizontalAlignment: Text.AlignHCenter;
                            verticalAlignment: Text.AlignVCenter
                        }
                        focusPolicy: Qt.NoFocus
                        // ... inside nextButton ...
onClicked: {
    var nextFile = fileManager.getNextFile()
    if (nextFile) {
        // 使用统一的加载方法
        mediaController.loadMedia(nextFile)
    }
}
                    }
                }

                // 5. 音量控制区 (最右边)
                Row {
                    width: 150
                    height: parent.height
                    spacing: 10

                    Text {
                        text: "🔊"
                        color: "white"
                        font.pixelSize: 16
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Slider {
                        id: volumeSlider
                        width: 100
                        anchors.verticalCenter: parent.verticalCenter
                        from: 0.0
                        to: 1.0
                        value: 1.0 // 默认 100%
                        focusPolicy: Qt.NoFocus // 防止滑块窃取焦点

                        // === 核心：调用 C++ setVolume ===
                        onValueChanged: {
                            mediaController.setVolume(value)
                        }

                        // 音量条样式 (简易版)
                        background: Rectangle {
                            x: volumeSlider.leftPadding
                            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                            implicitWidth: 200; implicitHeight: 4
                            width: volumeSlider.availableWidth; height: implicitHeight
                            radius: 2; color: "#555"
                            Rectangle {
                                width: volumeSlider.visualPosition * parent.width
                                height: parent.height
                                color: "#00FF00" // 绿色代表音量
                                radius: 2
                            }
                        }
                        handle: Rectangle {
                            x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                            implicitWidth: 14; implicitHeight: 14; radius: 7
                            color: "white"
                        }
                    }
                }
            }
        }
    }
}
