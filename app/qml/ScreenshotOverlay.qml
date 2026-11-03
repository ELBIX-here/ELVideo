import QtQuick
import QtQuick.Controls
import QtQuick.Window

// Screenshot overlay: every monitor shows its frozen image. Hover highlights the
// window under the cursor; click captures that window, drag captures a region.
// The toolbar captures the whole monitor or all monitors. Esc / right-click cancels.
Item {
    id: root

    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: screenCapture.active
        onActivated: screenCapture.cancel()
    }

    // Debug builds: keyboard-driven checks without the mouse (scripts/test/shot_test.ps1).
    // Ctrl+Shift+F1: region 300,200 801x451 (logical) on the primary monitor.
    // Ctrl+Shift+F2: click the centre of the window titled "ELV test window".
    function overlayOn(screen) {
        for (let i = 0; i < overlays.count; ++i)
            if (overlays.objectAt(i) && overlays.objectAt(i).screen === screen) return overlays.objectAt(i)
        return null
    }
    Shortcut {
        sequence: "Ctrl+Shift+F1"
        context: Qt.ApplicationShortcut
        enabled: debugBuild && screenCapture.active
        onActivated: screenCapture.captureRegion(root.overlayOn(Qt.application.screens[0]), Qt.rect(300, 200, 801, 451))
    }
    Shortcut {
        sequence: "Ctrl+Shift+F2"
        context: Qt.ApplicationShortcut
        enabled: debugBuild && screenCapture.active
        onActivated: {
            for (let i = 0; i < overlays.count; ++i) {
                const o = overlays.objectAt(i)
                const p = screenCapture.windowCenter(o, "ELV test window")
                if (p.x >= 0) {
                    o.hover = screenCapture.windowRectAt(o, p)  // the highlight the mouse would show
                    screenCapture.captureWindowAt(o, p)
                    return
                }
            }
        }
    }

    Instantiator {
        id: overlays
        model: screenCapture.active ? Qt.application.screens : []

        delegate: Window {
            id: overlay
            required property var modelData

            screen: modelData
            x: modelData.virtualX
            y: modelData.virtualY
            width: modelData.width
            height: modelData.height
            flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
            color: "black"
            title: "ELVideo capture"
            visible: true

            readonly property real ratio: modelData.devicePixelRatio
            property point start
            property bool pressed: false
            property bool dragging: false
            property rect selection: Qt.rect(0, 0, 0, 0)
            property rect hover: Qt.rect(0, 0, 0, 0)
            // What a release would capture: the dragged region, or the hovered window.
            readonly property rect target: {
                const r = dragging ? selection : hover
                const x0 = Math.max(0, r.x), y0 = Math.max(0, r.y)
                const x1 = Math.min(width, r.x + r.width), y1 = Math.min(height, r.y + r.height)
                return x1 > x0 && y1 > y0 ? Qt.rect(x0, y0, x1 - x0, y1 - y0) : Qt.rect(0, 0, 0, 0)
            }

            Component.onCompleted: {
                desktop.excludeFromCapture(overlay)
                frozen.source = screenCapture.frozenUrl(overlay)
                requestActivate()
            }

            Image {
                id: frozen
                anchors.fill: parent
                fillMode: Image.Stretch
                smooth: true
                cache: false
            }

            // Dim everything except the target.
            readonly property color dim: Qt.rgba(0, 0, 0, 0.4)
            Rectangle { x: 0; y: 0; width: parent.width; height: overlay.target.y; color: overlay.dim }
            Rectangle {
                x: 0; y: overlay.target.y + overlay.target.height
                width: parent.width; height: parent.height - y; color: overlay.dim
            }
            Rectangle {
                x: 0; y: overlay.target.y
                width: overlay.target.x; height: overlay.target.height; color: overlay.dim
            }
            Rectangle {
                x: overlay.target.x + overlay.target.width; y: overlay.target.y
                width: parent.width - x; height: overlay.target.height; color: overlay.dim
            }
            Rectangle {
                visible: overlay.target.width > 0
                x: overlay.target.x - 1
                y: overlay.target.y - 1
                width: overlay.target.width + 2
                height: overlay.target.height + 2
                color: "transparent"
                border.color: Theme.accent
                border.width: 2
            }
            Rectangle {
                visible: overlay.target.width > 0
                x: Math.min(overlay.target.x, overlay.width - width - 8)
                y: overlay.target.y > 34 ? overlay.target.y - 30 : overlay.target.y + overlay.target.height + 6
                width: sizeText.implicitWidth + 16
                height: 24
                radius: 6
                color: Theme.surface
                Text {
                    id: sizeText
                    anchors.centerIn: parent
                    text: Math.round(overlay.target.width * overlay.ratio) + " × " + Math.round(overlay.target.height * overlay.ratio)
                    color: Theme.text
                    font.family: Theme.monoFont
                    font.pixelSize: 13
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.CrossCursor
                onPressed: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        screenCapture.cancel()
                        return
                    }
                    overlay.start = Qt.point(mouse.x, mouse.y)
                    overlay.pressed = true
                }
                onPositionChanged: mouse => {
                    if (overlay.pressed) {
                        if (!overlay.dragging && Math.abs(mouse.x - overlay.start.x) + Math.abs(mouse.y - overlay.start.y) > 4)
                            overlay.dragging = true
                        if (overlay.dragging) {
                            const x = Math.max(0, Math.min(mouse.x, overlay.width))
                            const y = Math.max(0, Math.min(mouse.y, overlay.height))
                            overlay.selection = Qt.rect(Math.min(overlay.start.x, x), Math.min(overlay.start.y, y),
                                                        Math.abs(x - overlay.start.x), Math.abs(y - overlay.start.y))
                        }
                    } else {
                        overlay.hover = screenCapture.windowRectAt(overlay, Qt.point(mouse.x, mouse.y))
                    }
                }
                onReleased: mouse => {
                    if (mouse.button !== Qt.LeftButton || !overlay.pressed) return
                    overlay.pressed = false
                    if (overlay.dragging) {
                        overlay.dragging = false
                        if (overlay.selection.width >= 2 && overlay.selection.height >= 2)
                            screenCapture.captureRegion(overlay, overlay.selection)
                    } else {
                        screenCapture.captureWindowAt(overlay, Qt.point(mouse.x, mouse.y))
                    }
                }
            }

            // Toolbar, above the mouse area so its buttons get the clicks.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 16
                width: bar.implicitWidth + 24
                height: 48
                radius: 12
                color: Theme.surface
                border.color: Theme.border
                Row {
                    id: bar
                    anchors.centerIn: parent
                    spacing: 8
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("드래그: 영역 · 클릭: 창")
                        color: Theme.textDim
                        font.family: Theme.fontFamily
                        font.pixelSize: 13
                        rightPadding: 8
                    }
                    Button {
                        text: qsTr("이 모니터")
                        onClicked: screenCapture.captureMonitor(overlay)
                    }
                    Button {
                        text: qsTr("전체 화면")
                        onClicked: screenCapture.captureAll()
                    }
                    Button {
                        text: qsTr("취소 (Esc)")
                        onClicked: screenCapture.cancel()
                    }
                }
            }
        }
    }
}
