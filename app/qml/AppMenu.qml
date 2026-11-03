import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// Context menu: a floating rounded panel; items highlight in blue. Use with AppMenuItem.
Menu {
    id: menu

    padding: 5
    font.family: Theme.fontFamily
    font.pixelSize: 13

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.fast; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 100 } }

    delegate: AppMenuItem {}

    background: Rectangle {
        implicitWidth: 200
        radius: Theme.radius
        color: Theme.surfaceRaised
        border.color: Theme.dark ? "#3A3A3C" : "#D8D8DD"
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 0.8
            shadowVerticalOffset: 4
            autoPaddingEnabled: true
        }
    }
}
