import QtQuick
import QtQuick.Controls

// Seek bar: played part, A-B loop range, chapter ticks, a time tip on hover.
// Dragging seeks to keyframes while moving and exactly on release.
Item {
    id: bar

    property var player
    readonly property double duration: player ? player.duration : 0
    readonly property double shown: dragging ? dragTime : (player ? player.position : 0)
    property bool dragging: false
    property double dragTime: 0

    implicitHeight: 22

    function timeAt(x) { return duration > 0 ? Math.max(0, Math.min(1, x / width)) * duration : 0 }
    function xAt(t) { return duration > 0 ? t / duration * width : 0 }
    function format(seconds) {
        const s = Math.max(0, Math.floor(seconds))
        const pad = n => (n < 10 ? "0" : "") + n
        return (s >= 3600 ? Math.floor(s / 3600) + ":" : "") + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60)
    }

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: area.containsMouse || bar.dragging ? 6 : 4
        radius: height / 2
        color: Qt.rgba(1, 1, 1, 0.22)
        Behavior on height { NumberAnimation { duration: 100 } }

        Rectangle {  // A-B loop range
            visible: bar.player && bar.player.loopA >= 0
            x: bar.xAt(bar.player ? bar.player.loopA : 0)
            width: bar.player && bar.player.loopB >= 0 ? bar.xAt(bar.player.loopB) - x : 3
            height: parent.height
            color: Qt.rgba(0.95, 0.66, 0.23, 0.55)
        }
        Rectangle {  // played
            width: bar.xAt(bar.shown)
            height: parent.height
            radius: parent.radius
            color: Theme.record
        }
        Repeater {  // chapters
            model: bar.player ? bar.player.chapters : []
            Rectangle {
                required property var modelData
                x: bar.xAt(modelData.time) - 1
                width: 2
                height: track.height
                color: Qt.rgba(0, 0, 0, 0.6)
            }
        }
    }
    Rectangle {  // handle
        visible: bar.duration > 0 && (area.containsMouse || bar.dragging)
        x: bar.xAt(bar.shown) - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        radius: 7
        color: Theme.text
    }

    // Preview above the cursor: a frame near the hovered time (keyframe) and the time.
    Rectangle {
        id: preview
        visible: (area.containsMouse || bar.dragging) && bar.duration > 0
        readonly property double tipTime: bar.dragging ? bar.dragTime : bar.timeAt(area.mouseX)
        readonly property bool hasFrame: seekThumbs.source.length > 0 && Math.abs(seekThumbs.time - tipTime) < 15
        onTipTimeChanged: if (visible) seekThumbs.request(Math.round(tipTime * 2) / 2)
        x: Math.max(0, Math.min(bar.width - width, (bar.dragging ? bar.xAt(bar.dragTime) : area.mouseX) - width / 2))
        y: -height - 8
        width: hasFrame ? seekThumbs.thumbWidth + 6 : tip.implicitWidth + 14
        height: hasFrame ? seekThumbs.thumbHeight + 30 : 24
        radius: 6
        color: Theme.surface
        border.color: Theme.border
        Image {
            visible: preview.hasFrame
            x: 3
            y: 3
            width: seekThumbs.thumbWidth
            height: seekThumbs.thumbHeight
            source: seekThumbs.source
            cache: false
            asynchronous: false
        }
        Text {
            id: tip
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            text: bar.format(preview.tipTime)
            color: Theme.text
            font.family: Theme.monoFont
            font.pixelSize: 12
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        anchors.topMargin: -6
        anchors.bottomMargin: -6
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: bar.duration > 0
        onPressed: mouse => {
            bar.dragging = true
            bar.dragTime = bar.timeAt(mouse.x)
            bar.player.command(["seek", bar.dragTime.toFixed(3), "absolute+keyframes"])
        }
        onPositionChanged: mouse => {
            if (!bar.dragging) return
            bar.dragTime = bar.timeAt(mouse.x)
            bar.player.command(["seek", bar.dragTime.toFixed(3), "absolute+keyframes"])
        }
        onReleased: mouse => {
            bar.player.seek(bar.timeAt(mouse.x))
            bar.dragging = false
        }
    }
}
