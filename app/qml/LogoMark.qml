import QtQuick

// The ELVideo mark (app/resources/elvideo.svg) built from primitives so the
// reels can be animated. Geometry is in the SVG's 512x512 units.
// wind: 0 draws the icon exactly; -1 puts more film on the upper reel.
Item {
    id: root

    property bool showBackground: true
    property real topAngle: 0
    property real bottomAngle: 0
    property real wind: 0
    property real dotOpacity: 1

    readonly property color ink: "#16181F"
    readonly property color paper: "#ECE8E1"
    readonly property color amber: "#F2A93B"
    readonly property color red: "#E5484D"

    readonly property real topRadius: 46 - 6 * wind
    readonly property real bottomRadius: 70 + 6 * wind

    implicitWidth: 512
    implicitHeight: 512

    component Disc: Rectangle {
        property real cx
        property real cy
        property real r
        x: cx - r
        y: cy - r
        width: 2 * r
        height: 2 * r
        radius: r
        antialiasing: true
    }

    Item {
        id: canvas
        width: 512
        height: 512
        scale: Math.min(root.width, root.height) / 512
        transformOrigin: Item.TopLeft
        x: (root.width - 512 * scale) / 2
        y: (root.height - 512 * scale) / 2

        Rectangle {
            visible: root.showBackground
            anchors.fill: parent
            radius: 112
            color: root.ink
            antialiasing: true
        }

        Rectangle { x: 156; y: 58; width: 200; height: 16; radius: 8; color: root.paper; antialiasing: true }
        Rectangle { x: 156; y: 438; width: 200; height: 16; radius: 8; color: root.paper; antialiasing: true }

        // Film running from the upper reel's edge into the lower reel. It lies under
        // both reels, so the turning reels' holes never show it through.
        Rectangle {
            readonly property real x1: 256 - 44 * root.topRadius / 46
            readonly property real y1: 168 + 8 * root.topRadius / 46
            readonly property real x2: 300
            readonly property real y2: 336
            width: Math.hypot(x2 - x1, y2 - y1) + 10
            height: 10
            radius: 5
            x: (x1 + x2) / 2 - width / 2
            y: (y1 + y2) / 2 - height / 2
            rotation: Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI
            color: root.amber
            antialiasing: true
        }

        // Upper (supply) reel.
        Item {
            x: 256
            y: 168
            rotation: root.topAngle
            Disc { cx: 0; cy: 0; r: root.topRadius; color: root.amber }
            Disc { cx: 0; cy: -28; r: 8; color: root.ink }
            Disc { cx: -24; cy: 14; r: 8; color: root.ink }
            Disc { cx: 24; cy: 14; r: 8; color: root.ink }
            Disc { cx: 0; cy: 0; r: 10; color: root.paper }
        }

        // Lower (take-up) reel.
        Item {
            x: 256
            y: 344
            rotation: root.bottomAngle
            Disc { cx: 0; cy: 0; r: root.bottomRadius; color: root.amber }
            Disc { cx: 0; cy: -40; r: 11; color: root.ink }
            Disc { cx: -35; cy: 20; r: 11; color: root.ink }
            Disc { cx: 35; cy: 20; r: 11; color: root.ink }
            Disc { cx: 0; cy: 0; r: 12; color: root.paper }
        }

        // SVG strokes are centered on the path; Rectangle borders sit inside.
        Disc { cx: 256; cy: 168; r: 90; color: "transparent"; border.width: 12; border.color: root.paper }
        Disc { cx: 256; cy: 344; r: 90; color: "transparent"; border.width: 12; border.color: root.paper }
        Disc { cx: 256; cy: 256; r: 19; color: root.red; border.width: 6; border.color: root.ink; opacity: root.dotOpacity }
    }
}
