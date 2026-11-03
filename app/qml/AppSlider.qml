import QtQuick
import QtQuick.Controls

// Thin track with a blue fill and a round white knob.
Slider {
    id: control

    implicitWidth: 180
    implicitHeight: 24
    hoverEnabled: true

    background: Item {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        Rectangle {
            anchors.fill: parent
            radius: 2
            color: Theme.fill
        }
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
            opacity: control.enabled ? 1 : 0.4
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 18
        height: 18
        radius: 9
        color: "#FFFFFF"
        border.color: control.visualFocus ? Theme.accentFocus : Theme.dark ? "transparent" : "#D2D2D7"
        border.width: control.visualFocus ? 2 : 1
        scale: control.pressed ? 1.08 : 1
        Behavior on scale { NumberAnimation { duration: Theme.fast } }
    }
}
