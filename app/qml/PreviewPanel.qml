import QtQuick
import QtQuick.Layouts

// Live view of exactly what goes into the file (cropping and overlays included),
// shown in the main window while recording. About 10 frames per second.
Item {
    id: panel

    property int frameNo: 0
    // Only while the window is actually on screen (hidden while recording by default).
    readonly property bool running: visible && recorder.active && Window.window !== null && Window.window.visible
                                    && Window.window.visibility !== Window.Minimized

    Timer {
        interval: 100
        repeat: true
        running: panel.running
        // One request at a time; skip a tick while the previous frame is still coming.
        onTriggered: if (image.status !== Image.Loading) panel.frameNo++
    }

    Rectangle {
        id: frame
        anchors.centerIn: parent
        readonly property real aspect: image.implicitWidth > 0 ? image.implicitWidth / image.implicitHeight : 16 / 9
        width: Math.min(parent.width, (parent.height - caption.height - 12) * aspect)
        height: width / aspect
        anchors.verticalCenterOffset: -(caption.height + 12) / 2
        radius: Theme.radius
        color: "#0B0C10"
        border.color: recorder.paused ? Theme.accent : Theme.record
        border.width: 2
        clip: true

        Image {
            id: image
            anchors.fill: parent
            anchors.margins: 2
            source: panel.frameNo > 0 ? "image://preview/" + panel.frameNo : ""
            sourceSize: Qt.size(Math.round(width * Screen.devicePixelRatio), Math.round(height * Screen.devicePixelRatio))
            fillMode: Image.PreserveAspectFit
            cache: false
            asynchronous: true
            retainWhileLoading: true
            smooth: true
        }
        Text {
            anchors.centerIn: parent
            visible: image.status !== Image.Ready
            text: qsTr("미리보기를 불러오는 중…")
            color: Theme.textFaint
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
        Rectangle {
            visible: recorder.paused
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.45)
            Text {
                anchors.centerIn: parent
                text: qsTr("일시정지됨")
                color: Theme.accent
                font.family: Theme.fontFamily
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
        }
    }

    Text {
        id: caption
        anchors.top: frame.bottom
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        text: qsTr("녹화 중인 화면 미리보기 · 파일에 들어가는 그대로입니다")
        color: Theme.textDim
        font.family: Theme.fontFamily
        font.pixelSize: 12
    }
}
