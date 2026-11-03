import QtQuick
import QtQuick.Controls

// Number field with a small up/down stepper on the right.
SpinBox {
    id: control

    implicitWidth: 200
    implicitHeight: 30
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: 13
    leftPadding: 12
    rightPadding: 34

    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    component Step: Rectangle {
        property bool pressed
        property string glyph
        width: 22
        height: 12
        radius: 4
        color: pressed ? Theme.pressed : "transparent"
        Icon { anchors.centerIn: parent; glyph: parent.glyph; size: 8; color: Theme.textDim }
    }

    up.indicator: Step {
        x: control.width - width - 6
        y: control.height / 2 - height
        pressed: control.up.pressed
        glyph: Theme.iconChevronUp
    }
    down.indicator: Step {
        x: control.width - width - 6
        y: control.height / 2
        pressed: control.down.pressed
        glyph: Theme.iconChevronDown
    }

    background: Rectangle {
        radius: Theme.radius - 2
        color: Theme.control
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.controlBorder
    }
}
