import QtQuick

// Red outline around the region being recorded. Click-through and excluded from
// capture, so it shows where the edges are without appearing in the video.
Window {
    id: frame

    property rect region: Qt.rect(0, 0, 0, 0)  // logical desktop coordinates
    readonly property int pad: 3

    x: region.x - pad
    y: region.y - pad
    width: region.width + 2 * pad
    height: region.height + 2 * pad
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowTransparentForInput
           | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    title: "ELVideo region frame"

    Component.onCompleted: desktop.excludeFromCapture(frame)

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.record
        border.width: 2
        radius: 2
    }
}
