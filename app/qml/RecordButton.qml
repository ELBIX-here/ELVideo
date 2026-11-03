import QtQuick
import QtQuick.Controls

// Record button in the style of the system recorders: a ring around a red dot
// that morphs into a rounded square while recording.
AbstractButton {
    id: control

    property bool recording
    property real diameter: 54
    property bool onHud: false

    implicitWidth: diameter
    implicitHeight: diameter
    hoverEnabled: true
    Accessible.name: recording ? qsTr("녹화 정지") : qsTr("녹화 시작")

    background: Rectangle {
        radius: width / 2
        color: "transparent"
        border.width: 3
        border.color: control.visualFocus ? Theme.accent
                    : control.onHud ? Qt.rgba(1, 1, 1, 0.35) : Theme.dark ? "#5A5A5E" : "#D1D1D6"
        opacity: control.enabled ? 1 : 0.4
    }

    contentItem: Item {
        Rectangle {
            anchors.centerIn: parent
            width: control.recording ? control.diameter * 0.40 : control.diameter - 14
            height: width
            radius: control.recording ? 6 : width / 2
            color: control.down ? Qt.darker(Theme.record, 1.2) : control.hovered ? Qt.lighter(Theme.record, 1.08) : Theme.record
            opacity: control.enabled ? 1 : 0.4
            Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutBack; easing.overshoot: 1.4 } }
            Behavior on radius { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: Theme.fast } }
        }
    }
    scale: down ? 0.95 : 1
    Behavior on scale { NumberAnimation { duration: Theme.fast } }
}
