import QtQuick
import QtQuick.Controls

// Round red button: a dot while idle, a rounded square while recording.
AbstractButton {
    id: control

    property bool recording
    property real diameter: 52

    implicitWidth: diameter
    implicitHeight: diameter
    hoverEnabled: true
    Accessible.name: recording ? qsTr("녹화 정지") : qsTr("녹화 시작")

    background: Rectangle {
        radius: width / 2
        color: control.down ? Qt.darker(Theme.record, 1.25) : control.hovered ? Qt.lighter(Theme.record, 1.12) : Theme.record
        opacity: control.enabled ? 1 : 0.4
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.text
        Behavior on color { ColorAnimation { duration: 100 } }

        // Soft pulse ring while recording.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width
            height: width
            radius: width / 2
            color: "transparent"
            border.color: Theme.record
            border.width: 2
            visible: control.recording
            SequentialAnimation on scale {
                running: control.recording
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 1.35; duration: 1100; easing.type: Easing.OutCubic }
                PauseAnimation { duration: 300 }
            }
            SequentialAnimation on opacity {
                running: control.recording
                loops: Animation.Infinite
                NumberAnimation { from: 0.8; to: 0; duration: 1100; easing.type: Easing.OutCubic }
                PauseAnimation { duration: 300 }
            }
        }
    }

    contentItem: Item {
        Rectangle {
            anchors.centerIn: parent
            width: control.recording ? control.diameter * 0.34 : control.diameter * 0.36
            height: width
            radius: control.recording ? 4 : width / 2
            color: Theme.text
            Behavior on radius { NumberAnimation { duration: 150 } }
            Behavior on width { NumberAnimation { duration: 150 } }
        }
    }
}
