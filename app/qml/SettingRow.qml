import QtQuick
import QtQuick.Layouts

// One row of a grouped settings list: label and description on the left, a
// control on the right, and a hairline above it when a row above is visible.
Item {
    id: row

    property string label
    property string description
    property string error
    default property alias control: holder.data

    // A row above in the same group is showing (rows hide with their toggles).
    readonly property bool hasRowAbove: {
        if (!parent) return false
        for (const sibling of parent.children) {
            if (sibling === row) return false
            if (sibling.visible) return true
        }
        return false
    }

    Layout.fillWidth: true
    implicitHeight: Math.max(44, content.implicitHeight + 22)

    Rectangle {
        visible: row.hasRowAbove
        x: 16
        width: parent.width - 16
        height: 1
        color: Theme.separator
    }

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 16
        anchors.rightMargin: 14
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
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
                lineHeight: 1.15
                wrapMode: Text.Wrap
            }
        }
        Item {
            id: holder
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }
}
