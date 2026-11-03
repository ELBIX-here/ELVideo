import QtQuick
import QtQuick.Controls

// One row of an AppMenu: check mark column, text, optional submenu chevron.
MenuItem {
    id: item

    property bool destructive: false

    implicitHeight: 28
    implicitWidth: Math.max(190, label.implicitWidth + 64)
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: 13

    indicator: Icon {
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: item.checked
        glyph: Theme.iconCheck
        size: 11
        color: item.highlighted ? Theme.accentText : Theme.text
    }
    arrow: Icon {
        x: item.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        visible: item.subMenu !== null
        glyph: ""
        size: 9
        color: item.highlighted ? Theme.accentText : Theme.textDim
    }
    contentItem: Text {
        id: label
        leftPadding: 22
        rightPadding: 22
        text: item.text
        font: item.font
        color: item.highlighted ? Theme.accentText : item.destructive ? Theme.danger : Theme.text
        opacity: item.enabled ? 1 : 0.35
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 6
        color: item.highlighted && item.enabled ? Theme.accent : "transparent"
    }
}
