import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Choose what to record: a display or a window.
Item {
    id: picker

    // The user wants to drag a new region (Main hides itself and opens the overlay).
    signal regionRequested()

    component Segment: AbstractButton {
        id: seg
        property bool current
        property int count
        property bool showCount: true
        hoverEnabled: true
        implicitHeight: 32
        implicitWidth: label.implicitWidth + 28
        contentItem: Text {
            id: label
            text: seg.showCount ? seg.text + "  " + seg.count : seg.text
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
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

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
                        text: qsTr("디스플레이")
                        count: sources.monitorCount
                        current: !sources.regionMode && !sources.showWindows
                        onClicked: {
                            sources.regionMode = false
                            sources.showWindows = false
                        }
                    }
                    Segment {
                        text: qsTr("창")
                        count: sources.windowCount
                        current: !sources.regionMode && sources.showWindows
                        onClicked: {
                            sources.regionMode = false
                            sources.showWindows = true
                        }
                    }
                    Segment {
                        text: qsTr("영역")
                        showCount: false
                        current: sources.regionMode
                        onClicked: {
                            if (sources.hasRegion) sources.regionMode = true
                            else picker.regionRequested()
                        }
                    }
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

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 520
                    height: 300
                    radius: Theme.radius + 2
                    color: "#0B0C10"
                    border.color: Theme.accent
                    border.width: 2
                    Image {
                        id: regionImage
                        anchors.fill: parent
                        anchors.margins: 4
                        source: sources.regionThumbnail
                        sourceSize: Qt.size(512, 292)
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
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    Button {
                        text: qsTr("영역 다시 선택…")
                        onClicked: picker.regionRequested()
                    }
                    Button {
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
