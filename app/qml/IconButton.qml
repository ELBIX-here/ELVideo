import QtQuick
import QtQuick.Controls

// Flat square button with an icon glyph and an optional tooltip.
AbstractButton {
    id: control

    property string glyph
    property string tip
    property color iconColor: Theme.text
    property real iconSize: 16

    implicitWidth: 36
    implicitHeight: 36
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: tip

    contentItem: Icon {
        glyph: control.glyph
        size: control.iconSize
        color: control.enabled ? control.iconColor : Theme.textFaint
    }
    background: Rectangle {
        radius: 8
        color: control.down ? Theme.border : control.hovered ? Theme.surfaceHover : "transparent"
        border.color: control.visualFocus ? Theme.accent : "transparent"
    }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 500
}
