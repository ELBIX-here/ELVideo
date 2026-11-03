import QtQuick
import QtQuick.Controls

// One row of the main window's sidebar: glyph, label, an optional count or
// badge on the right; tinted blue when it is the current section.
AbstractButton {
    id: item

    property string glyph
    property color glyphColor: Theme.text
    property bool current: false
    property string badge   // count or short status on the right
    property color badgeColor: Theme.textDim
    property bool addStyle: false  // "add" style: blue text, no selection

    implicitHeight: 32
    hoverEnabled: true
    Accessible.name: text

    contentItem: Row {
        spacing: 10
        leftPadding: 10
        rightPadding: 10
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            glyph: item.glyph
            size: 15
            color: item.current || item.addStyle ? Theme.accent : item.glyphColor
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: item.width - 56 - badgeText.implicitWidth
            text: item.text
            elide: Text.ElideRight
            color: item.current || item.addStyle ? Theme.accent : Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.body
            font.weight: item.current ? Font.DemiBold : Font.Normal
        }
        Text {
            id: badgeText
            anchors.verticalCenter: parent.verticalCenter
            text: item.badge
            color: item.badgeColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.caption
            font.features: { "tnum": 1 }
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: item.current ? Theme.accentSoft : item.down ? Theme.pressed : item.hovered ? Theme.surfaceHover : "transparent"
        border.width: item.visualFocus ? 2 : 0
        border.color: Theme.accentFocus
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
