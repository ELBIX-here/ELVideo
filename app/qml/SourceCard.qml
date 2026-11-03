import QtQuick
import QtQuick.Controls

// One monitor or window in the picker: the thumbnail with rounded corners, the
// title below it, and a blue ring around the thumbnail when selected.
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
        radius: Theme.cardRadius + 2
        color: card.hovered && !card.selected ? Theme.surfaceHover : "transparent"
        border.width: card.visualFocus ? 2 : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }

    contentItem: Column {
        spacing: 8
        padding: 8

        Item {
            id: frameArea
            width: card.width - 16
            height: Math.round(width * 9 / 16)

            // Selection ring, a little outside the picture.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: Theme.cardRadius
                color: "transparent"
                border.width: 3
                border.color: Theme.accent
                opacity: card.selected ? 1 : 0
                scale: card.selected ? 1 : 1.03
                Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                Behavior on scale { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
            }

            RoundedClip {
                id: frame
                anchors.fill: parent
                radius: Theme.radius - 2

                Rectangle { anchors.fill: parent; color: Theme.mediaBackground }

                // The last good thumbnail stays underneath while a refresh loads.
                Image {
                    id: previous
                    anchors.fill: parent
                    sourceSize: image.sourceSize
                    fillMode: Image.PreserveAspectFit
                    cache: true  // URLs change per refresh; lets "previous" reuse the pixmap
                    smooth: true
                    visible: !image.hasFrame
                }
                Image {
                    id: image
                    anchors.fill: parent
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
            }

            // Placeholder while loading, or when the source sends no frame (minimized window).
            Column {
                anchors.centerIn: parent
                spacing: 6
                visible: !image.hasFrame && !(previous.status === Image.Ready && previous.source != "")
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    glyph: card.kind === "monitor" ? Theme.iconMonitor : Theme.iconWindow
                    size: 26
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
            width: frameArea.width
            topPadding: 2
            spacing: 1
            Text {
                width: parent.width
                text: card.title
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                color: card.selected ? Theme.accent : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: card.subtitle
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
        }
    }
}
