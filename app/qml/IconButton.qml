import QtQuick
import QtQuick.Controls

// Borderless toolbar button: just the glyph, with a soft rounded highlight on hover.
AbstractButton {
    id: control

    property string glyph
    property string tip
    property color iconColor: Theme.text
    property real iconSize: 16
    property bool round: false        // circle instead of a rounded square
    property bool onHud: false        // sits on a dark HUD in either mode
    property bool checkedLook: false  // tinted, for toggles that are on

    implicitWidth: 34
    implicitHeight: 34
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: tip

    contentItem: Icon {
        glyph: control.glyph
        size: control.iconSize
        color: control.checkedLook ? Theme.accent : control.iconColor
        opacity: control.enabled ? 1 : 0.35
    }
    background: Rectangle {
        radius: control.round ? width / 2 : Theme.radius
        color: control.checkedLook ? Theme.accentSoft
             : control.down ? (control.onHud ? Qt.rgba(1, 1, 1, 0.16) : Theme.pressed)
             : control.hovered && control.enabled ? (control.onHud ? Theme.hudHover : Theme.surfaceHover)
             : "transparent"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.accentFocus
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    scale: down ? Theme.pressScale : 1
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 600
}
