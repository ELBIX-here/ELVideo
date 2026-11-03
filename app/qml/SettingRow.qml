import QtQuick
import QtQuick.Layouts

// Label and description on the left, a control on the right.
RowLayout {
    id: row

    property string label
    property string description
    property string error
    default property alias control: holder.data

    Layout.fillWidth: true
    spacing: 16

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text {
            Layout.fillWidth: true
            text: row.label
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: row.error.length > 0 ? row.error : row.description
            color: row.error.length > 0 ? Theme.danger : Theme.textDim
            font.family: Theme.fontFamily
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
    }
    Item {
        id: holder
        implicitWidth: childrenRect.width
        implicitHeight: childrenRect.height
    }
}
