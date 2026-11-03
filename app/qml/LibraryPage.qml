import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Recordings, replays and screenshots, newest first.
Item {
    id: page

    signal message(string text)

    component Segment: AbstractButton {
        id: seg
        property bool current
        property int count: -1
        hoverEnabled: true
        implicitHeight: 32
        implicitWidth: label.implicitWidth + 28
        contentItem: Text {
            id: label
            text: seg.count >= 0 ? seg.text + "  " + seg.count : seg.text
            color: seg.current ? Theme.background : seg.hovered ? Theme.text : Theme.textDim
            font.family: Theme.fontFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8
            color: seg.current ? Theme.accent : "transparent"
        }
    }

    function open(path, kind) {
        if (kind === "image") desktop.openFile(path)
        else desktop.openInPlayer(path)
    }

    onVisibleChanged: if (visible) library.refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                implicitWidth: segments.implicitWidth + 8
                implicitHeight: 40
                radius: 10
                color: Theme.surface
                border.color: Theme.border
                Row {
                    id: segments
                    anchors.centerIn: parent
                    spacing: 4
                    Segment {
                        text: qsTr("전체")
                        count: library.videoCount + library.imageCount
                        current: library.filter === "all"
                        onClicked: library.filter = "all"
                    }
                    Segment {
                        text: qsTr("동영상")
                        count: library.videoCount
                        current: library.filter === "video"
                        onClicked: library.filter = "video"
                    }
                    Segment {
                        text: qsTr("캡처")
                        count: library.imageCount
                        current: library.filter === "image"
                        onClicked: library.filter = "image"
                    }
                }
            }
            Item { Layout.fillWidth: true }
            IconButton {
                glyph: Theme.iconFolder
                tip: qsTr("녹화 폴더 열기")
                onClicked: desktop.openFolder(appSettings.outputDir || appSettings.defaultOutputDir)
            }
            IconButton {
                glyph: Theme.iconRefresh
                tip: qsTr("새로 고침")
                onClicked: library.refresh()
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: library
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            readonly property int columns: Math.max(1, Math.floor(width / 230))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round((cellWidth - 28) * 9 / 16) + 72

            delegate: Item {
                id: cell
                required property string path
                required property string name
                required property string kind
                required property string date
                required property string size
                required property string duration
                required property string thumbnail
                width: grid.cellWidth
                height: grid.cellHeight

                AbstractButton {
                    id: card
                    anchors.fill: parent
                    anchors.margins: 6
                    hoverEnabled: true
                    Accessible.name: cell.name
                    onDoubleClicked: page.open(cell.path, cell.kind)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: itemMenu.popup()
                    }
                    background: Rectangle {
                        radius: Theme.radius + 2
                        color: card.hovered ? Theme.surfaceHover : Theme.surface
                        border.color: Theme.border
                    }
                    contentItem: Column {
                        spacing: 6
                        padding: 8
                        Rectangle {
                            width: card.width - 16
                            height: Math.round(width * 9 / 16)
                            radius: Theme.radius - 2
                            color: "#0B0C10"
                            clip: true
                            Image {
                                id: thumb
                                anchors.fill: parent
                                source: cell.thumbnail
                                sourceSize: Qt.size(320, 180)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                readonly property bool hasFrame: status === Image.Ready && implicitWidth > 1
                                opacity: hasFrame ? 1 : 0
                            }
                            Icon {
                                anchors.centerIn: parent
                                visible: !thumb.hasFrame
                                glyph: cell.kind === "image" ? Theme.iconCamera : Theme.iconPlay
                                size: 26
                                color: Theme.textFaint
                            }
                            Rectangle {  // duration
                                visible: cell.duration.length > 0
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 6
                                width: durationText.implicitWidth + 10
                                height: 20
                                radius: 4
                                color: Qt.rgba(0, 0, 0, 0.7)
                                Text {
                                    id: durationText
                                    anchors.centerIn: parent
                                    text: cell.duration
                                    color: "white"
                                    font.family: Theme.monoFont
                                    font.pixelSize: 11
                                }
                            }
                            Rectangle {  // replay / capture badge
                                visible: cell.kind !== "video"
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 6
                                width: badge.implicitWidth + 10
                                height: 20
                                radius: 4
                                color: cell.kind === "replay" ? Theme.accent : Qt.rgba(0, 0, 0, 0.7)
                                Text {
                                    id: badge
                                    anchors.centerIn: parent
                                    text: cell.kind === "replay" ? qsTr("리플레이") : qsTr("캡처")
                                    color: cell.kind === "replay" ? Theme.background : "white"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                            }
                            Rectangle {  // play on hover
                                visible: card.hovered
                                anchors.centerIn: parent
                                width: 44
                                height: 44
                                radius: 22
                                color: Qt.rgba(0, 0, 0, 0.55)
                                Icon {
                                    anchors.centerIn: parent
                                    glyph: cell.kind === "image" ? Theme.iconSearch : Theme.iconPlay
                                    size: 18
                                }
                            }
                        }
                        Text {
                            width: card.width - 16
                            text: cell.name
                            elide: Text.ElideMiddle
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                        }
                        Text {
                            width: card.width - 16
                            text: cell.date + " · " + cell.size
                            elide: Text.ElideRight
                            color: Theme.textDim
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                        }
                    }
                    onClicked: page.open(cell.path, cell.kind)
                }
                Menu {
                    id: itemMenu
                    MenuItem {
                        text: cell.kind === "image" ? qsTr("열기") : qsTr("재생")
                        onTriggered: page.open(cell.path, cell.kind)
                    }
                    MenuItem {
                        text: qsTr("폴더에서 보기")
                        onTriggered: desktop.revealFile(cell.path)
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("휴지통으로 이동")
                        onTriggered: {
                            const name = cell.name
                            if (library.moveToTrash(cell.path)) page.message(qsTr("휴지통으로 옮겼습니다: %1").arg(name))
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: qsTr("아직 녹화나 캡처가 없습니다")
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: 13
            }
        }
    }
}
