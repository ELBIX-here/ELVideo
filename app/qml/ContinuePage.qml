import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Videos started but not finished, most recent first, from every folder: the
// place to pick up a lecture where you left it.
Item {
    id: page

    property real bottomInset: 0

    readonly property var items: progress.revision >= 0 && visible ? progress.inProgress(36) : []

    function ago(ms) {
        const minutes = Math.floor((Date.now() - ms) / 60000)
        if (minutes < 1) return qsTr("방금")
        if (minutes < 60) return qsTr("%1분 전").arg(minutes)
        const hours = Math.floor(minutes / 60)
        if (hours < 24) return qsTr("%1시간 전").arg(hours)
        return qsTr("%1일 전").arg(Math.floor(hours / 24))
    }

    GridView {
        id: grid
        anchors.fill: parent
        clip: true
        bottomMargin: page.bottomInset
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        model: page.items
        readonly property int columns: Math.max(1, Math.floor(width / 250))
        cellWidth: Math.floor(width / columns)
        cellHeight: Math.round((cellWidth - 36) * 9 / 16) + 104

        delegate: Item {
            id: cell
            required property var modelData
            width: grid.cellWidth
            height: grid.cellHeight

            AbstractButton {
                id: card
                anchors.fill: parent
                anchors.margins: 8
                hoverEnabled: true
                Accessible.name: cell.modelData.name
                onClicked: desktop.openInPlayer(cell.modelData.path, cell.modelData.parent || "", cell.modelData.folder)
                scale: down ? 0.98 : 1
                Behavior on scale { NumberAnimation { duration: Theme.fast } }
                background: Rectangle {
                    radius: Theme.cardRadius
                    color: Theme.surface
                    border.width: card.visualFocus ? 2 : 1
                    border.color: card.visualFocus ? Theme.accentFocus : Theme.border
                }
                contentItem: Column {
                    padding: 10
                    spacing: 3
                    RoundedClip {
                        width: card.width - 20
                        height: Math.round(width * 9 / 16)
                        radius: Theme.radius
                        Rectangle { anchors.fill: parent; color: Theme.mediaBackground }
                        Image {
                            anchors.fill: parent
                            source: Theme.thumbnailUrl(cell.modelData.path)
                            sourceSize: Qt.size(400, 225)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            scale: card.hovered ? 1.03 : 1
                            Behavior on scale { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
                        }
                        // Play on hover.
                        Rectangle {
                            anchors.centerIn: parent
                            width: 44
                            height: 44
                            radius: 22
                            color: Qt.rgba(0, 0, 0, 0.45)
                            border.color: Qt.rgba(1, 1, 1, 0.25)
                            opacity: card.hovered ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
                            Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: 1; glyph: Theme.iconPlay; size: 17; color: "white" }
                        }
                        // How far into it.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 4
                            color: Qt.rgba(0, 0, 0, 0.35)
                            Rectangle {
                                width: cell.modelData.duration > 0 ? parent.width * Math.min(1, cell.modelData.position / cell.modelData.duration) : 0
                                height: parent.height
                                color: Theme.accent
                            }
                        }
                    }
                    Item { width: 1; height: 6 }
                    Text {
                        leftPadding: 2
                        width: card.width - 20
                        text: cell.modelData.name
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.body
                        font.weight: Font.DemiBold
                    }
                    Text {
                        leftPadding: 2
                        width: card.width - 20
                        text: cell.modelData.folder + " · " + page.ago(cell.modelData.updated)
                        elide: Text.ElideRight
                        color: Theme.textDim
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fine
                    }
                    Text {
                        leftPadding: 2
                        topPadding: 4
                        text: qsTr("%1부터 이어서 재생 ›").arg(Theme.formatClock(cell.modelData.position))
                        color: Theme.accent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.caption
                    }
                }
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -page.bottomInset / 2
            visible: grid.count === 0
            spacing: 8
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("이어서 볼 동영상이 없습니다")
                color: Theme.text
                font.family: Theme.displayFont
                font.pixelSize: Theme.tagline
                font.weight: Font.DemiBold
                font.letterSpacing: Theme.tracking(Theme.tagline)
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("보다가 멈춘 동영상이 여기에 모입니다.")
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.body
            }
        }
    }
}
