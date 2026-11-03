import QtQuick

// Small audio level bar: glyph + bar on a -60..0 dBFS scale. Hidden for level < 0.
Row {
    id: meter

    property string glyph
    property real level: -1  // peak 0..1

    readonly property real fraction: level <= 0 ? 0 : Math.max(0, Math.min(1, (20 * Math.log10(level) + 60) / 60))

    visible: level >= 0
    spacing: 5

    Icon {
        anchors.verticalCenter: parent.verticalCenter
        glyph: meter.glyph
        size: 12
        color: Theme.textDim
    }
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 56
        height: 5
        radius: 2.5
        color: Theme.surfaceHover
        Rectangle {
            height: parent.height
            radius: parent.radius
            width: parent.width * meter.fraction
            color: meter.fraction > 0.95 ? Theme.danger : meter.fraction > 0.8 ? Theme.accent : Theme.success
            Behavior on width { NumberAnimation { duration: 120 } }
        }
    }
}
