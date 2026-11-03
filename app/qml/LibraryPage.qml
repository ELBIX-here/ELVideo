import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Recordings, replays and screenshots, newest first, as store-style cards:
// white (dark: tile) card, hairline, 18 px corners, the picture inset at 8 px.
Item {
    id: page

    signal message(string text)
    // "Start recording" from the empty state.
    signal recordRequested()
    // Room under the content for the floating record bar.
    property real bottomInset: 0

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
            SearchField {
                placeholderText: qsTr("이름으로 찾기")
                text: library.search
                onTextEdited: library.search = text
            }
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
            bottomMargin: page.bottomInset
            model: library
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            readonly property int columns: Math.max(1, Math.floor(width / 230))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round((cellWidth - 36) * 9 / 16) + 96

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
                    anchors.margins: 8
                    hoverEnabled: true
                    Accessible.name: cell.name
                    scale: down ? 0.98 : 1
                    Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }
                    onDoubleClicked: page.open(cell.path, cell.kind)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: itemMenu.popup()
                    }
                    background: Rectangle {
                        radius: Theme.cardRadius
                        color: Theme.surface
                        border.width: card.visualFocus ? 2 : 1
                        border.color: card.visualFocus ? Theme.accentFocus : Theme.border
                    }
                    contentItem: Column {
                        spacing: 2
                        padding: 10
                        RoundedClip {
                            width: card.width - 20
                            height: Math.round(width * 9 / 16)
                            radius: Theme.radius
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
                        Item { width: 1; height: 6 }
                        Text {
                            leftPadding: 2
                            width: card.width - 20
                            text: cell.name
                            elide: Text.ElideMiddle
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.body
                            font.weight: Font.DemiBold
                        }
                        Text {
                            leftPadding: 2
                            width: card.width - 20
                            text: cell.date + " · " + cell.size
                            elide: Text.ElideRight
                            color: Theme.textDim
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fine
                        }
                        Text {  // text link, the card's action
                            leftPadding: 2
                            topPadding: 4
                            text: (cell.kind === "image" ? qsTr("열기") : qsTr("재생")) + " \u203A"
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.caption
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

            // Empty state: a quiet headline and one pill action.
            Column {
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -page.bottomInset / 2
                visible: grid.count === 0
                spacing: 10
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: library.search.length > 0 ? qsTr("찾는 항목이 없습니다") : qsTr("아직 녹화나 캡처가 없습니다")
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: Theme.tagline
                    font.weight: Font.DemiBold
                    font.letterSpacing: Theme.tracking(Theme.tagline)
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: library.search.length > 0 ? qsTr("다른 이름으로 찾아 보세요.")
                                                    : qsTr("녹화한 동영상과 캡처한 화면이 여기에 모입니다.")
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.body
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: library.search.length === 0
                    primary: true
                    text: qsTr("녹화하러 가기")
                    onClicked: page.recordRequested()
                }
            }
        }
    }
}
