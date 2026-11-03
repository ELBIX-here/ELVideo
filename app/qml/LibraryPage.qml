import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Recordings, replays and screenshots, newest first.
Item {
    id: page

    signal message(string text)

    function open(path, kind) {
        if (kind === "image") desktop.openFile(path)
        else desktop.openInPlayer(path)
    }

    onVisibleChanged: if (visible) library.refresh()

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            SegmentedControl {
                readonly property var keys: ["all", "video", "image"]
                segments: [
                    { text: qsTr("전체"), count: library.videoCount + library.imageCount },
                    { text: qsTr("동영상"), count: library.videoCount },
                    { text: qsTr("캡처"), count: library.imageCount }
                ]
                currentIndex: Math.max(0, keys.indexOf(library.filter))
                onActivated: index => library.filter = keys[index]
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
            cellHeight: Math.round((cellWidth - 28) * 9 / 16) + 66

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
                        radius: Theme.cardRadius + 2
                        color: card.hovered ? Theme.surfaceHover : "transparent"
                        border.width: card.visualFocus ? 2 : 0
                        border.color: Theme.accent
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                    contentItem: Column {
                        spacing: 4
                        padding: 8
                        RoundedClip {
                            width: card.width - 16
                            height: Math.round(width * 9 / 16)
                            radius: Theme.radius - 2
                            Rectangle { anchors.fill: parent; color: Theme.mediaBackground }
                            Image {
                                id: thumb
                                anchors.fill: parent
                                source: cell.thumbnail
                                sourceSize: Qt.size(320, 180)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                readonly property bool hasFrame: status === Image.Ready && implicitWidth > 1
                                opacity: hasFrame ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: 180 } }
                                scale: card.hovered ? 1.03 : 1
                                Behavior on scale { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
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
                                width: durationText.implicitWidth + 12
                                height: 18
                                radius: 9
                                color: Qt.rgba(0, 0, 0, 0.55)
                                Text {
                                    id: durationText
                                    anchors.centerIn: parent
                                    text: cell.duration
                                    color: "white"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    font.features: { "tnum": 1 }
                                }
                            }
                            Rectangle {  // replay / capture badge
                                visible: cell.kind !== "video"
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 6
                                width: badge.implicitWidth + 12
                                height: 18
                                radius: 9
                                color: cell.kind === "replay" ? Theme.accent : Qt.rgba(0, 0, 0, 0.55)
                                Text {
                                    id: badge
                                    anchors.centerIn: parent
                                    text: cell.kind === "replay" ? qsTr("리플레이") : qsTr("캡처")
                                    color: "white"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                            }
                            Rectangle {  // play on hover
                                anchors.centerIn: parent
                                width: 44
                                height: 44
                                radius: 22
                                color: Qt.rgba(0, 0, 0, 0.45)
                                border.color: Qt.rgba(1, 1, 1, 0.25)
                                opacity: card.hovered ? 1 : 0
                                scale: card.hovered ? 1 : 0.85
                                Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                                Behavior on scale { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutBack } }
                                Icon {
                                    anchors.centerIn: parent
                                    anchors.horizontalCenterOffset: cell.kind === "image" ? 0 : 1
                                    glyph: cell.kind === "image" ? Theme.iconSearch : Theme.iconPlay
                                    size: 17
                                    color: "white"
                                }
                            }
                        }
                        Item { width: 1; height: 4 }
                        Text {
                            width: card.width - 16
                            text: cell.name
                            elide: Text.ElideMiddle
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
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
                AppMenu {
                    id: itemMenu
                    AppMenuItem {
                        text: cell.kind === "image" ? qsTr("열기") : qsTr("재생")
                        onTriggered: page.open(cell.path, cell.kind)
                    }
                    AppMenuItem {
                        text: qsTr("폴더에서 보기")
                        onTriggered: desktop.revealFile(cell.path)
                    }
                    AppMenuSeparator {}
                    AppMenuItem {
                        destructive: true
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
