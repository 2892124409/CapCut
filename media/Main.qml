import QtQuick
import QtQuick.Controls
import "."

Window {
    width: 1000
    height: 700
    visible: true
    title: qsTr("剪映实习生 - 视频播放器 (C++ FFmpeg)")
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint

    MediaView {
        anchors.fill: parent
    }
}
