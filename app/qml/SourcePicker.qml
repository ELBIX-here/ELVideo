import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Choose what to record: a display or a window.
Item {
    id: picker

    component Segment: AbstractButton {
        id: seg
        property bool current
        property int count
        hoverEnabled: true
        implicitHeight: 32
        implicitWidth: label.implicitWidth + 28
        contentItem: Text {
            id: label
            text: seg.text + "  " + seg.count
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
                        current: !sources.showWindows
                        onClicked: sources.showWindows = false
                    }
                    Segment {
                        text: qsTr("창")
                        count: sources.windowCount
                        current: sources.showWindows
                        onClicked: sources.showWindows = true
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

        GridView {
            id: grid
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
