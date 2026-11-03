import QtQuick
import QtQuick.Controls

// Push button in the DESIGN.md grammar:
//   primary - blue pill, the one action that matters on a surface
//   ghost   - outlined blue pill, the second action next to a primary
//   default - compact utility rectangle (pearl fill, hairline ring)
// Every kind shrinks to Theme.pressScale while pressed.
Button {
    id: control

    property bool primary: false
    property bool ghost: false
    readonly property bool pill: primary || ghost

    implicitHeight: pill ? 32 : 30
    implicitWidth: Math.max(pill ? 84 : 64, contentItem.implicitWidth + (pill ? 36 : 28))
    hoverEnabled: true
    padding: 0
    font.family: Theme.fontFamily
    font.pixelSize: Theme.body

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.primary ? Theme.accentText : control.ghost ? Theme.accent : Theme.text
        opacity: control.enabled ? 1 : 0.4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: control.pill ? height / 2 : Theme.radius
        color: control.primary ? (control.hovered ? Qt.lighter(Theme.accent, 1.08) : Theme.accent)
             : control.ghost ? (control.hovered ? Theme.accentSoft : "transparent")
             : control.down ? Theme.pressed : control.hovered ? Theme.surfaceHover : Theme.control
        border.width: control.visualFocus ? 2 : control.primary ? 0 : 1
        border.color: control.visualFocus ? Theme.accentFocus : control.ghost ? Theme.accent : Theme.controlBorder
        opacity: control.enabled ? 1 : 0.6
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }

    scale: down ? Theme.pressScale : 1
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }
}
