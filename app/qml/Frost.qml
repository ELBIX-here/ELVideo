import QtQuick
import QtQuick.Effects

// Frosted glass (DESIGN.md "backdrop blur"): a blurred, desaturated copy of
// whatever `source` shows under this item, clipped to `radius` and tinted.
// Elevation for floating bars comes from this, not from a shadow.
//
// Put it next to `source` (same parent) and give it the floating bar's
// geometry; the bar itself goes on top with a transparent background. The blur
// takes `source` as its input directly (laid over the source's whole area and
// clipped to this item), so texture providers like the mpv video item work too.
Item {
    id: root

    property Item source
    property real radius: Theme.cardRadius
    property color tint: Theme.frost
    property color borderColor: Theme.border
    property color base: Theme.background  // what the source sits on

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
            x: root.source ? root.source.x - root.x : 0
            y: root.source ? root.source.y - root.y : 0
            width: root.source ? root.source.width : 0
            height: root.source ? root.source.height : 0
            source: root.source
            blurEnabled: true
            blurMax: 48
            blur: 1.0
            saturation: 0.3
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.tint
        border.color: root.borderColor
    }
}
