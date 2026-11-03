import QtQuick

// Startup splash: a flat card with the app icon and the name. The hourglass's
// reels turn, winding film from the upper reel to the lower one, and stop on
// the exact icon pose; the record light comes on and the name rises in, then
// the card fades away. Click to skip.
Window {
    id: splash

    signal finished()

    width: 340
    height: 360
    flags: Qt.SplashScreen | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    transientParent: null
    title: "ELVideo"

    Component.onCompleted: {
        x = screen.virtualX + (screen.width - width) / 2
        y = screen.virtualY + (screen.height - height) / 2
        visible = true
    }

    function finish() {
        if (done) return
        done = true
        timeline.stop()
        splash.finished()
    }
    property bool done: false

    // The card fades as one image; per-item opacity would let overlapping
    // shapes show through each other.
    Item {
        id: card
        anchors.fill: parent
        anchors.margins: 20
        opacity: 0
        scale: 0.97
        layer.enabled: true

        Rectangle {
            anchors.fill: parent
            radius: Theme.cardRadius
            color: Theme.surface
            border.color: Theme.border
        }

        // The icon rests on the card with the system's one shadow.
        ProductShadow {
            anchors.fill: logo
            radius: logo.width * 114 / 512
            color: logo.tile
        }
        LogoMark {
            id: logo
            width: 120
            height: 120
            anchors.horizontalCenter: parent.horizontalCenter
            y: 58
            wind: -1
            dotOpacity: 0
        }

        Text {
            id: wordmark
            anchors.horizontalCenter: parent.horizontalCenter
            y: logo.y + logo.height + 26
            text: "ELVideo"
            color: Theme.text
            font.family: Theme.displayFont
            font.pixelSize: Theme.largeTitle
            font.weight: Font.DemiBold
            font.letterSpacing: Theme.tracking(Theme.largeTitle)
            opacity: 0
        }
        Text {
            id: tagline
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: wordmark.bottom
            anchors.topMargin: 4
            text: qsTr("녹화 · 캡처 · 재생")
            color: Theme.textDim
            font.family: Theme.fontFamily
            font.pixelSize: Theme.body
            opacity: 0
        }

        MouseArea {
            anchors.fill: parent
            onClicked: splash.finish()
        }
    }

    SequentialAnimation {
        id: timeline
        running: true

        // The card settles in.
        ParallelAnimation {
            NumberAnimation { target: card; property: "opacity"; to: 1; duration: 260; easing.type: Easing.OutCubic }
            NumberAnimation { target: card; property: "scale"; to: 1; duration: 420; easing.type: Easing.OutCubic }
        }
        // The reels wind film down; the upper one turns faster because it is smaller.
        // End angles are multiples of 120 deg so the holes land where the icon has them.
        // The name rises in meanwhile, and the record light comes on as they stop.
        ParallelAnimation {
            NumberAnimation { target: logo; property: "topAngle"; from: 0; to: -480; duration: 1300; easing.type: Easing.InOutCubic }
            NumberAnimation { target: logo; property: "bottomAngle"; from: 0; to: 360; duration: 1300; easing.type: Easing.InOutCubic }
            NumberAnimation { target: logo; property: "wind"; from: -1; to: 0; duration: 1300; easing.type: Easing.InOutCubic }
            SequentialAnimation {
                PauseAnimation { duration: 300 }
                ParallelAnimation {
                    NumberAnimation { target: wordmark; property: "opacity"; to: 1; duration: 420; easing.type: Easing.OutCubic }
                    NumberAnimation { target: wordmark; property: "y"; from: logo.y + logo.height + 36; to: logo.y + logo.height + 26; duration: 520; easing.type: Easing.OutCubic }
                }
            }
            SequentialAnimation {
                PauseAnimation { duration: 460 }
                NumberAnimation { target: tagline; property: "opacity"; to: 1; duration: 420; easing.type: Easing.OutCubic }
            }
            SequentialAnimation {
                PauseAnimation { duration: 1150 }
                NumberAnimation { target: logo; property: "dotOpacity"; to: 1; duration: 220; easing.type: Easing.OutCubic }
            }
        }

        PauseAnimation { duration: 500 }

        ParallelAnimation {
            NumberAnimation { target: card; property: "opacity"; to: 0; duration: 300; easing.type: Easing.InCubic }
            NumberAnimation { target: card; property: "scale"; to: 0.98; duration: 300; easing.type: Easing.InCubic }
        }

        ScriptAction { script: splash.finish() }
    }
}
