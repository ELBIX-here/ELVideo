import QtQuick
import QtQuick.Controls

// Hairline between groups of AppMenu items.
MenuSeparator {
    topPadding: 4
    bottomPadding: 4
    leftPadding: 10
    rightPadding: 10
    contentItem: Rectangle {
        implicitHeight: 1
        color: Theme.separator
    }
}
