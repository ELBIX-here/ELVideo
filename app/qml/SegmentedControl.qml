import QtQuick
import QtQuick.Effects

// Apple-style segmented control: a gray track with a raised thumb that slides
// to the current segment. `segments` is a list of { text, count? }.
Rectangle {
    id: root

    property var segments: []
    property int currentIndex: 0
    signal activated(int index)

    implicitWidth: row.implicitWidth + 4
    implicitHeight: 32
    radius: Theme.radius - 1
    color: Theme.fill

    Rectangle {
        id: thumb
        readonly property Item target: row.children[root.currentIndex] ?? null
        visible: target !== null
        x: target ? target.x + 2 : 2
        y: 2
        width: target ? target.width : 0
        height: root.height - 4
        radius: Theme.radius - 3
        color: Theme.thumb
        Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
        layer.enabled: !Theme.dark
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.12)
            shadowBlur: 0.3
            shadowVerticalOffset: 1
            autoPaddingEnabled: true
        }
    }

    Row {
        id: row
        x: 2
        height: parent.height
        Repeater {
            model: root.segments
            delegate: MouseArea {
                id: seg
                required property int index
                required property var modelData
                readonly property bool current: index === root.currentIndex
                width: label.implicitWidth + 32
                height: row.height
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Button
                Accessible.name: modelData.text
                Accessible.onPressAction: root.activated(index)
                onClicked: root.activated(index)

                Text {
                    id: label
                    anchors.centerIn: parent
                    textFormat: Text.StyledText
                    text: seg.modelData.count !== undefined
                          ? seg.modelData.text + "  <font color='" + Theme.textDim + "'>" + seg.modelData.count + "</font>"
                          : seg.modelData.text
                    color: seg.current || seg.containsMouse ? Theme.text : Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    font.weight: seg.current ? Font.DemiBold : Font.Normal
                }
                // Hairline divider between unselected neighbors.
                Rectangle {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 1
                    height: 14
                    color: Theme.textFaint
                    opacity: 0.35
                    visible: seg.index < root.segments.length - 1 && !seg.current && seg.index + 1 !== root.currentIndex
                }
            }
        }
    }
}
