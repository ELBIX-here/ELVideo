import QtQuick
import QtQuick.Controls

// On/off switch in the app colors (the Basic style one is gray in both states).
Switch {
    id: control

    implicitWidth: 44
    implicitHeight: 24
    padding: 0
    spacing: 0

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.surfaceHover
        border.color: control.checked ? Theme.accent : control.visualFocus ? Theme.text : Theme.border
        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            radius: 9
            color: control.checked ? Theme.background : Theme.textDim
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
}
