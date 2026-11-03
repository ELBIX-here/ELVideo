import QtQuick
import QtQuick.Controls

// Push button: a white (dark: gray) rounded pill, or blue when `primary`.
Button {
    id: control

    property bool primary: false

    implicitHeight: 30
    implicitWidth: Math.max(72, contentItem.implicitWidth + 28)
    hoverEnabled: true
    padding: 0
    font.family: Theme.fontFamily
    font.pixelSize: 13

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? Theme.accentText : Theme.text
        opacity: control.enabled ? 1 : 0.4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radius - 2
        color: control.primary ? (control.down ? Qt.darker(Theme.accent, 1.15) : control.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
                               : (control.down ? Theme.pressed : control.hovered && Theme.dark ? Qt.lighter(Theme.control, 1.15) : Theme.control)
        border.width: control.visualFocus ? 2 : control.primary ? 0 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.controlBorder
        opacity: control.enabled ? 1 : 0.6
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
