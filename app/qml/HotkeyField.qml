import QtQuick
import QtQuick.Controls

// Click, then press a key combination. Esc cancels, Backspace/Delete clears.
// Global hotkeys are suspended while editing so the combination reaches us.
AbstractButton {
    id: field

    property string sequence
    signal edited(string sequence)

    readonly property bool editing: activeFocus

    implicitWidth: 200
    implicitHeight: 30
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: sequence

    onClicked: forceActiveFocus()
    onEditingChanged: hotkeys.suspended = editing

    Keys.onPressed: event => {
        event.accepted = true
        if (event.key === Qt.Key_Escape) {
            focus = false
            return
        }
        if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
            field.edited("")
            focus = false
            return
        }
        const text = hotkeys.sequenceFromKey(event.key, event.modifiers)
        if (text.length > 0) {
            field.edited(text)
            focus = false
        }
    }

    contentItem: Text {
        text: field.editing ? qsTr("키 조합을 누르세요…")
                            : (field.sequence.length > 0 ? field.sequence : qsTr("없음"))
        color: field.editing ? Theme.accent : field.sequence.length > 0 ? Theme.text : Theme.textFaint
        font.family: Theme.fontFamily
        font.pixelSize: 13
        font.weight: field.sequence.length > 0 && !field.editing ? Font.DemiBold : Font.Normal
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: Theme.radius
        color: field.editing ? Theme.accentSoft : field.hovered && Theme.dark ? Qt.lighter(Theme.control, 1.15) : Theme.control
        border.width: field.editing ? 2 : 1
        border.color: field.editing ? Theme.accent : Theme.controlBorder
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
}
