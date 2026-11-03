import QtQuick
import QtQuick.Controls

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
        radius: Theme.radiusMd
        color: Theme.surfaceRaised
        border.color: Theme.popupBorder
    }
}
