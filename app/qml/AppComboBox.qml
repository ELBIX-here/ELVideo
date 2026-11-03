import QtQuick
import QtQuick.Controls

// Popup button: the current choice with an up/down chevron; the list opens as a
// floating rounded panel with a check mark on the current item.
ComboBox {
    id: control

    // Optional (index) => bool; disabled items stay visible but cannot be picked.
    property var itemEnabled: null

    implicitHeight: 30
    implicitWidth: 200
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: 13

    contentItem: Text {
        leftPadding: 12
        rightPadding: 30
        text: control.displayText
        font: control.font
        color: Theme.text
        opacity: control.enabled ? 1 : 0.4
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Rectangle {
        x: control.width - width - 5
        anchors.verticalCenter: parent.verticalCenter
        width: 20
        height: 20
        radius: 6
        color: Theme.accent
        opacity: control.enabled ? 1 : 0.4
        Column {
            anchors.centerIn: parent
            spacing: -3
            Icon { glyph: Theme.iconChevronUp; size: 7; color: Theme.accentText; anchors.horizontalCenter: parent.horizontalCenter }
            Icon { glyph: Theme.iconChevronDown; size: 7; color: Theme.accentText; anchors.horizontalCenter: parent.horizontalCenter }
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.pressed : control.hovered && Theme.dark ? Qt.lighter(Theme.control, 1.15) : Theme.control
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accentFocus : Theme.controlBorder
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }

    scale: down ? Theme.pressScale : 1
    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }

    delegate: ItemDelegate {
        id: item
        required property int index
        required property var modelData
        width: ListView.view ? ListView.view.width : control.width
        height: 28
        hoverEnabled: true
        enabled: !control.itemEnabled || control.itemEnabled(index)
        highlighted: control.highlightedIndex === index
        contentItem: Row {
            spacing: 6
            leftPadding: 4
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                width: 14
                glyph: Theme.iconCheck
                size: 11
                color: item.highlighted ? Theme.accentText : Theme.text
                opacity: control.currentIndex === item.index ? 1 : 0
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.textRole ? item.modelData[control.textRole] : item.modelData
                font: control.font
                color: item.highlighted ? Theme.accentText : Theme.text
                opacity: item.enabled ? 1 : 0.35
            }
        }
        background: Rectangle {
            radius: 6
            color: item.highlighted && item.enabled ? Theme.accent : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: Math.max(control.width, 160)
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 360)
        padding: 5

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
                NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.fast; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 100 } }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.surfaceRaised
            border.color: Theme.popupBorder
        }
    }
}
