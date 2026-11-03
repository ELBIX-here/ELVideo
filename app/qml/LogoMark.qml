import QtQuick

// The ELVideo mark (app/resources/elvideo.svg): an hourglass whose two bulbs
// hold film reels — less on the upper, more on the lower — and the record dot
// at the waist, white and red on a dark tile. Built from primitives so the
// splash can animate it. Geometry is in the SVG's 512x512 units.
//   wind: 0 draws the icon exactly; -1 puts more film on the upper reel
//   topAngle / bottomAngle: reel rotation (multiples of 120 land on the icon pose)
Item {
    id: root

    property bool showBackground: true
    property real topAngle: 0
    property real bottomAngle: 0
    property real wind: 0
    property real dotOpacity: 1

    readonly property color tile: "#1D1D1F"
    readonly property color paper: "#FFFFFF"
    readonly property color red: "#FF3B30"

    // Film moves from the upper reel (44 at rest) to the lower one (56 at rest).
    readonly property real topRadius: 44 - 14 * wind
    readonly property real bottomRadius: 56 + 16 * wind

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

    // A reel: a white disc with three holes and a hub, turning about its center.
    component Reel: Item {
        id: reel
        property real radius
        property real hole
        property real angle
        rotation: angle
        Disc { cx: 0; cy: 0; r: reel.radius; color: root.paper }
        Repeater {
            model: 3
            Disc {
                required property int index
                readonly property real a: (index * 120 - 90) * Math.PI / 180
                cx: Math.cos(a) * reel.radius * 0.55
                cy: Math.sin(a) * reel.radius * 0.55
                r: reel.hole
                color: root.tile
            }
        }
    }

    Item {
        width: 512
        height: 512
        scale: Math.min(root.width, root.height) / 512
        transformOrigin: Item.TopLeft
        x: (root.width - 512 * scale) / 2
        y: (root.height - 512 * scale) / 2

        Rectangle {
            visible: root.showBackground
            anchors.fill: parent
            radius: 114
            color: root.tile
            antialiasing: true
        }

        // The hourglass's top and bottom plates.
        Rectangle { x: 164; y: 64; width: 184; height: 18; radius: 9; color: root.paper; antialiasing: true }
        Rectangle { x: 164; y: 430; width: 184; height: 18; radius: 9; color: root.paper; antialiasing: true }

        Reel { x: 256; y: 186; radius: root.topRadius; hole: 9; angle: root.topAngle }
        Reel { x: 256; y: 326; radius: root.bottomRadius; hole: 11; angle: root.bottomAngle }

        // The two bulbs. SVG strokes are centered on the path; Rectangle borders sit inside.
        Disc { cx: 256; cy: 186; r: 90; color: "transparent"; border.width: 20; border.color: root.paper }
        Disc { cx: 256; cy: 326; r: 90; color: "transparent"; border.width: 20; border.color: root.paper }

        // The record light at the waist, ringed in the tile color so it sits apart from the bulbs.
        Disc { cx: 256; cy: 256; r: 28; color: root.red; border.width: 8; border.color: root.tile; opacity: root.dotOpacity }
    }
}
