import QtQuick
import QtQuick.Window

// Full-screen overlay on every monitor: drag a rectangle to choose the recording
// region. Esc or right-click cancels. Emits finished(true) once a region is set.
Item {
    id: selector

    signal finished(bool accepted)

    property bool active: false

    function open() { active = true }
    function close(accepted) {
        if (!active) return
        active = false
        finished(accepted)
    }

    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: selector.active
        onActivated: selector.close(false)
    }

    Instantiator {
        model: selector.active ? Qt.application.screens : []

        delegate: Window {
            id: overlay
            required property var modelData

            screen: modelData
            x: modelData.virtualX
            y: modelData.virtualY
            width: modelData.width
            height: modelData.height
            flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
            color: "transparent"
            title: "ELVideo region"
            visible: true

            property point start
            property rect selection: Qt.rect(0, 0, 0, 0)
            property bool dragging: false
            readonly property real ratio: modelData.devicePixelRatio

            Component.onCompleted: {
                desktop.excludeFromCapture(overlay)
                requestActivate()
            }

            // Dim everything except the selection.
            readonly property color dim: Qt.rgba(0, 0, 0, 0.45)
            Rectangle { x: 0; y: 0; width: parent.width; height: overlay.selection.y; color: overlay.dim }
            Rectangle {
                x: 0; y: overlay.selection.y + overlay.selection.height
                width: parent.width; height: parent.height - y; color: overlay.dim
            }
            Rectangle {
                x: 0; y: overlay.selection.y
                width: overlay.selection.x; height: overlay.selection.height; color: overlay.dim
            }
            Rectangle {
                x: overlay.selection.x + overlay.selection.width; y: overlay.selection.y
                width: parent.width - x; height: overlay.selection.height; color: overlay.dim
            }

            Rectangle {
                x: overlay.selection.x - 1
                y: overlay.selection.y - 1
                width: overlay.selection.width + 2
                height: overlay.selection.height + 2
                visible: overlay.selection.width > 0
                color: "transparent"
                border.color: Theme.accent
                border.width: 2
            }

            // Size in physical pixels, as it will be recorded.
            Rectangle {
                visible: overlay.selection.width > 0
                x: Math.min(overlay.selection.x, overlay.width - width - 8)
                y: overlay.selection.y > 34 ? overlay.selection.y - 30 : overlay.selection.y + overlay.selection.height + 6
                width: sizeLabel.implicitWidth + 16
                height: 24
                radius: 6
                color: Theme.surface
                Text {
                    id: sizeLabel
                    anchors.centerIn: parent
                    text: Math.round(overlay.selection.width * overlay.ratio) + " × "
                          + Math.round(overlay.selection.height * overlay.ratio)
                    color: Theme.text
                    font.family: Theme.monoFont
                    font.pixelSize: 13
                }
            }

            Rectangle {
                anchors.centerIn: parent
                visible: !overlay.dragging && overlay.selection.width === 0
                width: hint.implicitWidth + 40
                height: 48
                radius: 12
                color: Theme.surface
                border.color: Theme.border
                Text {
                    id: hint
                    anchors.centerIn: parent
                    text: qsTr("드래그해서 녹화할 영역을 고르세요   ·   Esc 취소")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.CrossCursor
                onPressed: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        selector.close(false)
                        return
                    }
                    overlay.start = Qt.point(mouse.x, mouse.y)
                    overlay.selection = Qt.rect(mouse.x, mouse.y, 0, 0)
                    overlay.dragging = true
                }
                onPositionChanged: mouse => {
                    if (!overlay.dragging) return
                    const x = Math.max(0, Math.min(mouse.x, overlay.width))
                    const y = Math.max(0, Math.min(mouse.y, overlay.height))
                    overlay.selection = Qt.rect(Math.min(overlay.start.x, x), Math.min(overlay.start.y, y),
                                                Math.abs(x - overlay.start.x), Math.abs(y - overlay.start.y))
                }
                onReleased: mouse => {
                    if (mouse.button !== Qt.LeftButton || !overlay.dragging) return
                    overlay.dragging = false
                    if (sources.setRegionFromOverlay(overlay, overlay.selection))
                        selector.close(true)
                    else
                        overlay.selection = Qt.rect(0, 0, 0, 0)  // too small: try again
                }
            }
        }
    }
}
