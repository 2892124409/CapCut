import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Item {
    id: mediaView
    focus: true

    // 暴露内部对象，方便宿主项目直接访问
    property alias controller: mediaController
    property alias manager: fileManager
    property alias volumeSlider: volumeSlider

    // 保持焦点，方便键盘快捷键永远生效
    onFocusChanged: {
        if (!focus) {
            focus = true
        }
    }

    Keys.onPressed: (event) => {
        switch(event.key) {
        case Qt.Key_Space:
            if (mediaController.paused) mediaController.play()
            else mediaController.pause()
            event.accepted = true
            break
        case Qt.Key_Left:
            if (mediaController.duration > 0) {
                var seekBack = Math.max(0, mediaController.position - 5000)
                mediaController.seek(seekBack)
            }
            event.accepted = true
            break
        case Qt.Key_Right:
            if (mediaController.duration > 0) {
                var seekForward = Math.min(mediaController.duration, mediaController.position + 5000)
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
            volumeSlider.value = volumeSlider.value > 0 ? 0 : 1.0
            event.accepted = true
            break
        case Qt.Key_F: {
            const rootWindow = mediaView.window
            if (!rootWindow)
                break
            if (rootWindow.visibility === Window.FullScreen) {
                rootWindow.showNormal()
            } else {
                rootWindow.showFullScreen()
            }
            event.accepted = true
            break
        }
        case Qt.Key_Escape: {
            const rootWindow = mediaView.window
            if (rootWindow && rootWindow.visibility === Window.FullScreen) {
                rootWindow.showNormal()
                event.accepted = true
            }
            break
        }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: mediaView.forceActiveFocus()
        hoverEnabled: false
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

        MediaController {
            id: mediaController
            anchors.fill: parent
        }
    }

    FileManager {
        id: fileManager
    }

    // 文件信息显示（顶部）
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
                id: fileCountText
                text: fileManager.videoFiles.length > 0 ?
                      "文件 " + (fileManager.currentIndex + 1) + "/" + fileManager.videoFiles.length : ""
                color: "#aaa"
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "请选择媒体文件"
        nameFilters: [
            "媒体文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.3gp *.ts *.mp3 *.wav *.flac *.aac *.ogg *.m4a *.wma *.opus *.aiff *.ape *.jpg *.jpeg *.png *.bmp *.gif *.tiff *.tif *.webp *.ico *.svg)",
            "视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v *.3gp *.ts)",
            "图片文件 (*.jpg *.jpeg *.png *.bmp *.gif *.tiff *.tif *.webp *.ico *.svg)",
            "音频文件 (*.mp3 *.wav *.flac *.aac *.ogg *.m4a *.wma *.opus *.aiff *.ape)",
            "所有文件 (*)"
        ]
        onAccepted: {
            var filePath = fileDialog.selectedFile.toString().replace("file:///", "")
            loadMediaFromPath(filePath)
        }
    }

    Rectangle {
        id: controls
        height: isImage(fileManager.currentFile) ? 60 : 90
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

            Item {
                id: progressBarContainer
                width: parent.width
                height: 30
                visible: !isImage(fileManager.currentFile)

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

                    MouseArea {
                        id: progressHandleArea
                        anchors.fill: parent
                        hoverEnabled: true
                        drag.target: progressHandle
                        drag.axis: Drag.XAxis
                        drag.minimumX: 0
                        drag.maximumX: progressBackground.width

                        property bool wasPlayingBeforeDrag: false

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

            Row {
                width: parent.width
                height: 40
                spacing: 15

                Button {
                    text: "📁 选择视频文件"
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
                    onClicked: fileDialog.open()
                }

                Item {
                    width: parent.width - 140 - 150 - 20
                    height: parent.height

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
                        onClicked: {
                            var prevFile = fileManager.getPreviousFile()
                            if (prevFile) {
                                mediaController.loadMedia(prevFile)
                            }
                        }
                    }

                    Button {
                        id: playPauseButton
                        anchors.centerIn: parent
                        width: 140; height: 40
                        text: isImage(fileManager.currentFile) ? "🖼 查看图片" : (mediaController.paused ? "▶ 播 放" : "⏸ 暂 停")
                        font.bold: true; font.pixelSize: 16
                        background: Rectangle { color: parent.down ? "#333" : "#444"; radius: 8; border.color: "#555" }
                        contentItem: Text { text: parent.text; font: parent.font; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        focusPolicy: Qt.NoFocus
                        enabled: !isImage(fileManager.currentFile)
                        onClicked: {
                            if (mediaController.paused) mediaController.play();
                            else mediaController.pause();
                        }
                    }

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
                        onClicked: {
                            var nextFile = fileManager.getNextFile()
                            if (nextFile) {
                                mediaController.loadMedia(nextFile)
                            }
                        }
                    }
                }

                Row {
                    width: 150
                    height: parent.height
                    spacing: 10
                    visible: !isImage(fileManager.currentFile)

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
                        value: 1.0
                        focusPolicy: Qt.NoFocus

                        onValueChanged: mediaController.setVolume(value)

                        background: Rectangle {
                            x: volumeSlider.leftPadding
                            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                            implicitWidth: 200; implicitHeight: 4
                            width: volumeSlider.availableWidth; height: implicitHeight
                            radius: 2; color: "#555"
                            Rectangle {
                                width: volumeSlider.visualPosition * parent.width
                                height: parent.height
                                color: "#00FF00"
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

    function isImage(path) {
        if (!path || path.length === 0)
            return false
        const lower = path.toLowerCase()
        return lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png") ||
               lower.endsWith(".bmp") || lower.endsWith(".gif") || lower.endsWith(".tiff") ||
               lower.endsWith(".tif") || lower.endsWith(".webp") || lower.endsWith(".ico") ||
               lower.endsWith(".svg")
    }

    function loadMediaFromPath(filePath) {
        if (!filePath || filePath.length === 0)
            return
        fileManager.scanFolderForFile(filePath)
        mediaController.loadMedia(filePath)
    }
}
