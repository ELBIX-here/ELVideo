import QtQuick
import QtQuick.Controls

// On/off switch: a gray (on: blue) pill with a white knob that slides, like the system one.
Switch {
    id: control

    implicitWidth: 42
    implicitHeight: 24
    padding: 0
    spacing: 0
    hoverEnabled: true

    indicator: Rectangle {
        implicitWidth: 42
        implicitHeight: 24
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.fill
        opacity: control.enabled ? 1 : 0.45
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accentFocus
        Behavior on color { ColorAnimation { duration: Theme.normal } }

        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            anchors.verticalCenter: parent.verticalCenter
            // The knob stretches a little while pressed.
            width: control.pressed ? 24 : 20
            height: 20
            radius: 10
            color: "#FFFFFF"
            border.width: Theme.dark ? 0 : 1
            border.color: Qt.rgba(0, 0, 0, 0.08)
            Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutBack; easing.overshoot: 1.2 } }
            Behavior on width { NumberAnimation { duration: Theme.fast } }
        }
    }
    contentItem: Item {}
}
