import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Choose what to record: a display or a window.
Item {
    id: picker

    // The user wants to drag a new region (Main hides itself and opens the overlay).
    signal regionRequested()

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            SegmentedControl {
                segments: [
                    { text: qsTr("디스플레이"), count: sources.monitorCount },
                    { text: qsTr("창"), count: sources.windowCount },
                    { text: qsTr("영역") }
                ]
                currentIndex: sources.regionMode ? 2 : sources.showWindows ? 1 : 0
                onActivated: index => {
                    if (index === 2) {
                        if (sources.hasRegion) sources.regionMode = true
                        else picker.regionRequested()
                        return
                    }
                    sources.regionMode = false
                    sources.showWindows = index === 1
                }
            }
            Item { Layout.fillWidth: true }
            IconButton {
                glyph: Theme.iconRefresh
                tip: qsTr("새로 고침")
                onClicked: sources.refresh()
            }
        }

        // Region tab: the chosen rectangle and a button to choose again.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: sources.regionMode

            Column {
                anchors.centerIn: parent
                spacing: 14

                RoundedClip {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 520
                    height: 300
                    radius: Theme.cardRadius
                    Rectangle { anchors.fill: parent; color: Theme.mediaBackground }
                    Image {
                        id: regionImage
                        anchors.fill: parent
                        source: sources.regionThumbnail
                        sourceSize: Qt.size(1040, 600)
                        fillMode: Image.PreserveAspectFit
                        cache: false
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: regionImage.status !== Image.Ready
                        text: regionImage.status === Image.Loading ? qsTr("불러오는 중…") : qsTr("미리보기 없음")
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: sources.regionTitle
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    AppButton {
                        text: qsTr("영역 다시 선택…")
                        onClicked: picker.regionRequested()
                    }
                    AppButton {
                        primary: true
                        text: qsTr("녹화")
                        onClicked: recorder.start()
                    }
                }
            }
        }

        GridView {
            id: grid
            visible: !sources.regionMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sources
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            readonly property int columns: Math.max(1, Math.floor(width / 240))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.round((cellWidth - 12 - 16) * 9 / 16) + 72

            delegate: Item {
                required property string sourceId
                required property string kind
                required property string title
                required property string subtitle
                required property string thumbnail
                required property bool selected
                width: grid.cellWidth
                height: grid.cellHeight

                SourceCard {
                    anchors.fill: parent
                    anchors.margins: 6
                    kind: parent.kind
                    title: parent.title
                    subtitle: parent.subtitle
                    thumbnail: parent.thumbnail
                    selected: parent.selected
                    onClicked: sources.selectedId = parent.sourceId
                    onDoubleClicked: {
                        sources.selectedId = parent.sourceId
                        recorder.start()
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: sources.showWindows ? qsTr("녹화할 수 있는 창이 없습니다") : qsTr("디스플레이를 찾지 못했습니다")
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: 13
            }
        }
    }
}
