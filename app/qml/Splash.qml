import QtQuick

// Startup splash: a quiet card with the app icon, whose reels wind film from
// the upper to the lower reel and settle on the exact icon pose, and the name
// below it. The card settles in, holds briefly, and fades away. Click to skip.
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

    readonly property int spinDuration: 1700

    function finish() {
        if (done) return
        done = true
        timeline.stop()
        splash.finished()
    }
    property bool done: false

    // The card fades as one image; per-item opacity would let overlapping
    // shapes (reel over film) show through each other.
    Item {
        id: card
        anchors.fill: parent
        anchors.margins: 24
        opacity: 0
        scale: 0.96
        layer.enabled: true

        Rectangle {
            anchors.fill: parent
            radius: Theme.cardRadius
            color: Theme.surface
            border.color: Theme.border
        }

        ProductShadow {
            x: logo.x + logo.width * 6 / 512
            y: logo.y + logo.height * 6 / 512
            width: logo.width * 500 / 512
            height: logo.height * 500 / 512
            radius: width * 112 / 512
            color: "#16181F"
        }

        // The app icon, as on the desktop: the mark on its dark rounded square.
        LogoMark {
            id: logo
            width: 132
            height: 132
            anchors.horizontalCenter: parent.horizontalCenter
            y: 50
            wind: -1
        }

        Text {
            id: wordmark
            anchors.horizontalCenter: parent.horizontalCenter
            y: logo.y + logo.height + 24
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
            font.pixelSize: 13
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

        ParallelAnimation {
            // Card settles in.
            NumberAnimation { target: card; property: "opacity"; to: 1; duration: 280; easing.type: Easing.OutCubic }
            NumberAnimation { target: card; property: "scale"; to: 1; duration: 500; easing.type: Easing.OutBack; easing.overshoot: 1.1 }

            // Film winds down; the upper reel turns faster because it is smaller.
            // End angles are multiples of 120 deg so the holes land where the icon has them.
            NumberAnimation {
                target: logo; property: "topAngle"; from: 0; to: -720
                duration: splash.spinDuration; easing.type: Easing.InOutCubic
            }
            NumberAnimation {
                target: logo; property: "bottomAngle"; from: 0; to: 480
                duration: splash.spinDuration; easing.type: Easing.InOutCubic
            }
            NumberAnimation {
                target: logo; property: "wind"; from: -1; to: 0
                duration: splash.spinDuration; easing.type: Easing.InOutCubic
            }

            // Name rises in.
            SequentialAnimation {
                PauseAnimation { duration: 350 }
                ParallelAnimation {
                    NumberAnimation { target: wordmark; property: "opacity"; to: 1; duration: 500; easing.type: Easing.OutCubic }
                    NumberAnimation { target: wordmark; property: "y"; from: logo.y + logo.height + 34; to: logo.y + logo.height + 24; duration: 600; easing.type: Easing.OutCubic }
                }
            }
            SequentialAnimation {
                PauseAnimation { duration: 600 }
                NumberAnimation { target: tagline; property: "opacity"; to: 1; duration: 500; easing.type: Easing.OutCubic }
            }

            // The record light blinks once as the reels settle.
            SequentialAnimation {
                PauseAnimation { duration: splash.spinDuration - 300 }
                NumberAnimation { target: logo; property: "dotOpacity"; to: 0.2; duration: 220; easing.type: Easing.InOutQuad }
                NumberAnimation { target: logo; property: "dotOpacity"; to: 1; duration: 260; easing.type: Easing.InOutQuad }
            }
        }

        PauseAnimation { duration: 250 }

        ParallelAnimation {
            NumberAnimation { target: card; property: "opacity"; to: 0; duration: 320; easing.type: Easing.InCubic }
            NumberAnimation { target: card; property: "scale"; to: 0.98; duration: 320; easing.type: Easing.InCubic }
        }

        ScriptAction { script: splash.finish() }
    }
}
