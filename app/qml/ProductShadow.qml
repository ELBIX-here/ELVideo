import QtQuick
import QtQuick.Effects

// The one shadow in the system (DESIGN.md), under a picture resting on a
// surface — thumbnails, previews, the splash icon. Fill the picture's area with
// it and put the picture on top; never use it under chrome (bars, cards, buttons).
//
// Light: a 1 px contact edge plus a wide, faint ambient shadow, so the picture
// lifts without the smudge one big blur leaves. Dark: shadows vanish on a dark
// ground, so a bright hairline separates the picture instead.
Item {
    id: root

    property real radius: Theme.radius
    property color color: Theme.mediaBackground

    // Ambient: wide and faint, dropped a little.
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.color
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, Theme.dark ? 0.35 : 0.08)
            shadowVerticalOffset: 8
            blurMax: 24
            shadowBlur: 0.85  // about 20 px
            autoPaddingEnabled: true
        }
    }
    // Contact: tight, right at the edge.
    Rectangle {
        visible: !Theme.dark
        anchors.fill: parent
        radius: root.radius
        color: root.color
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.08)
            shadowVerticalOffset: 1
            blurMax: 4
            shadowBlur: 0.6
            autoPaddingEnabled: true
        }
    }
    // Ring: the edge itself, above the picture so white pictures keep a border.
    Rectangle {
        z: 10
        parent: root.parent
        x: root.x
        y: root.y
        width: root.width
        height: root.height
        visible: root.visible
        radius: root.radius
        color: "transparent"
        border.width: 1
        border.color: Theme.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.05)
    }
}
