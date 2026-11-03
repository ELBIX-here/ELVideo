import QtQuick
import QtQuick.Controls

// One monitor or window in the picker: live-ish thumbnail plus title.
AbstractButton {
    id: card

    property string kind
    property string title
    property string subtitle
    property string thumbnail
    property bool selected

    hoverEnabled: true
    Accessible.name: title

    background: Rectangle {
        radius: Theme.radius + 2
        color: card.selected ? Theme.surfaceRaised : card.hovered ? Theme.surfaceHover : Theme.surface
        border.width: card.selected ? 2 : 1
        border.color: card.selected ? Theme.accent : card.visualFocus ? Theme.textDim : Theme.border
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    contentItem: Column {
        spacing: 8
        padding: 8

        Rectangle {
            id: frame
            width: card.width - 16
            height: Math.round(width * 9 / 16)
            radius: Theme.radius - 2
            color: "#0B0C10"
            clip: true

            // The last good thumbnail stays underneath while a refresh loads.
            Image {
                id: previous
                anchors.fill: parent
                anchors.margins: 1
                sourceSize: image.sourceSize
                fillMode: Image.PreserveAspectFit
                cache: true  // URLs change per refresh; lets "previous" reuse the pixmap
                smooth: true
                visible: !image.hasFrame
            }
            Image {
                id: image
                anchors.fill: parent
                anchors.margins: 1
                source: card.thumbnail
                sourceSize: Qt.size(400, 225)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true  // URLs change per refresh; lets "previous" reuse the pixmap
                smooth: true
                // The provider sends a 1x1 image when the source has no frame.
                readonly property bool hasFrame: status === Image.Ready && implicitWidth > 1
                opacity: hasFrame ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
                onStatusChanged: {
                    if (status === Image.Ready) previous.source = hasFrame ? source : ""  // "" once minimized
                }
            }

            // Placeholder while loading, or when the source sends no frame (minimized window).
            Column {
                anchors.centerIn: parent
                spacing: 6
                visible: !image.hasFrame && !(previous.status === Image.Ready && previous.source != "")
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    glyph: card.kind === "monitor" ? Theme.iconMonitor : Theme.iconWindow
                    size: 28
                    color: Theme.textFaint
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: image.status === Image.Loading ? qsTr("불러오는 중…") : qsTr("미리보기 없음")
                    color: Theme.textFaint
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }
            }
        }

        Column {
            width: frame.width
            spacing: 2
            Text {
                width: parent.width
                text: card.title
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: card.subtitle
                elide: Text.ElideRight
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
        }
    }
}
