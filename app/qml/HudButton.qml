import QtQuick
import QtQuick.Controls

// Text (and optional glyph) button for the dark HUD toolbars over the screen.
AbstractButton {
    id: control

    property string glyph

    implicitHeight: 32
    implicitWidth: row.implicitWidth + 24
    hoverEnabled: true
    Accessible.name: text

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.glyph.length > 0
                glyph: control.glyph
                size: 13
                color: control.checked ? "#2997FF" : Theme.hudText
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                color: control.checked ? "#2997FF" : Theme.hudText
                font.family: Theme.fontFamily
                font.pixelSize: 13
            }
        }
    }
    background: Rectangle {
        radius: height / 2
        color: control.checked ? Qt.rgba(0.161, 0.592, 1, 0.22)  // on: Sky Link Blue tint
             : control.down ? Qt.rgba(1, 1, 1, 0.18) : control.hovered ? Theme.hudHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    scale: down ? Theme.pressScale : 1
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }
}
