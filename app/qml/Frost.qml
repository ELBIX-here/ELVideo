import QtQuick
import QtQuick.Effects

// Frosted glass (DESIGN.md "backdrop blur"): a blurred, desaturated copy of
// whatever `source` shows under this item, clipped to `radius` and tinted.
// `source` must not contain this item. Elevation for floating bars comes from
// this, not from a shadow.
//
// The blur takes `source` itself as its input (laid over the source's whole
// area and clipped to this item) rather than a ShaderEffectSource grab, so
// texture providers like the mpv video item work too.
Item {
    id: root

    property Item source
    property real radius: Theme.cardRadius
    property color tint: Theme.frost
    property color borderColor: Theme.border
    property color base: Theme.background  // what the source sits on
    // An OpenGL framebuffer item (the mpv video) hands out its texture bottom-up.
    property bool flipSource: false

    // Where this item sits inside the source; re-evaluated when either moves.
    readonly property point offset: {
        if (!source) return Qt.point(0, 0)
        void [x, y, width, height, source.width, source.height, parent ? parent.y : 0]
        return mapToItem(source, 0, 0)
    }

    RoundedClip {
        anchors.fill: parent
        radius: root.radius

        // Opaque base: the source is transparent where it shows the window
        // color, and the real (sharp) page behind must not show through there.
        Rectangle {
            anchors.fill: parent
            color: root.base
        }
        MultiEffect {
            visible: root.source !== null
            x: -root.offset.x
            y: -root.offset.y
            width: root.source ? root.source.width : 0
            height: root.source ? root.source.height : 0
            source: root.source
            blurEnabled: true
            blurMax: 48
            blur: 1.0
            saturation: 0.3
            transform: Scale { origin.y: root.source ? root.source.height / 2 : 0; yScale: root.flipSource ? -1 : 1 }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.tint
        border.color: root.borderColor
    }
}
