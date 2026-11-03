import QtQuick
import QtQuick.Controls

// Small always-on-top bar shown while recording. Excluded from screen capture,
// so it never appears in the video. Drag it by the grip on the left.
Window {
    id: bar

    width: 236
    height: 48
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    title: "ELVideo"

    property bool placed: false

    function place() {
        // Top center of the screen it is on; kept where the user drags it afterwards.
        if (placed) return
        x = screen.virtualX + (screen.width - width) / 2
        y = screen.virtualY + 16
        placed = true
    }

    onVisibleChanged: if (visible) place()
    Component.onCompleted: desktop.excludeFromCapture(bar)

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.hud
        border.color: Qt.rgba(1, 1, 1, 0.08)

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 8
            spacing: 8

            // Grip
            Item {
                width: 16
                height: 32
                Grid {
                    anchors.centerIn: parent
                    columns: 2
                    spacing: 3
                    Repeater {
                        model: 6
                        Rectangle { width: 3; height: 3; radius: 1.5; color: Qt.rgba(1, 1, 1, 0.3) }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    onPressed: bar.startSystemMove()
                }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
                radius: 5
                color: recorder.replayMode ? Theme.accent : recorder.paused ? Theme.warning : Theme.record
                property real blink: 1
                opacity: recorder.state === "recording" ? blink : 1
                SequentialAnimation on blink {
                    running: recorder.state === "recording"
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 600 }
                    NumberAnimation { to: 1; duration: 600 }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 84
                text: recorder.replayMode ? Theme.formatTime(recorder.replayBufferedSeconds * 1000)
                                          : Theme.formatTime(recorder.elapsedMs)
                color: Theme.hudText
                font.family: Theme.displayFont
                font.pixelSize: 16
                font.features: { "tnum": 1 }
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32
                glyph: recorder.replayMode ? Theme.iconSave : recorder.paused ? Theme.iconPlay : Theme.iconPause
                iconColor: recorder.replayMode ? Theme.accent : Theme.hudText
                round: true
                onHud: true
                tip: recorder.replayMode ? qsTr("리플레이 저장") : recorder.paused ? qsTr("다시 시작") : qsTr("일시정지")
                enabled: recorder.active
                onClicked: recorder.replayMode ? recorder.saveReplay() : recorder.togglePause()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32
                glyph: Theme.iconStop
                iconColor: Theme.record
                round: true
                onHud: true
                tip: qsTr("정지")
                enabled: recorder.active
                onClicked: recorder.stop()
            }
        }
    }
}
