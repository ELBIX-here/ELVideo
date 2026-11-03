import QtQuick
import QtQuick.Controls

// Pill-shaped search input with a leading glyph and a clear button.
TextField {
    id: field

    implicitWidth: 220
    implicitHeight: 32
    leftPadding: 34
    rightPadding: text.length > 0 ? 30 : 14
    font.family: Theme.fontFamily
    font.pixelSize: Theme.body
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    verticalAlignment: TextInput.AlignVCenter
    Keys.onEscapePressed: text = ""

    background: Rectangle {
        radius: height / 2
        color: Theme.control
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.accentFocus : Theme.controlBorder

        Icon {
            x: 13
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.iconSearch
            size: 13
            color: Theme.textDim
        }
        AbstractButton {
            visible: field.text.length > 0
            x: parent.width - width - 6
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 22
            Accessible.name: qsTr("지우기")
            onClicked: field.text = ""
            contentItem: Icon { glyph: Theme.iconClose; size: 9; color: Theme.textDim }
            background: Rectangle { radius: 11; color: Theme.fill }
        }
    }
}
