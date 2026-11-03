import QtQuick

// Startup splash: the reels wind film from the upper to the lower reel and land
// on the exact icon pose while the wordmark fades in, then everything fades out.
// Click to skip.
Window {
    id: splash

    signal finished()

    width: 360
    height: 400
    flags: Qt.SplashScreen | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    transientParent: null
    title: "ELVideo"

    Component.onCompleted: {
        x = screen.virtualX + (screen.width - width) / 2
        y = screen.virtualY + (screen.height - height) / 2
        visible = true
    }

    readonly property int spinDuration: 2400

    function finish() {
        if (done) return
        done = true
        timeline.stop()
        splash.finished()
    }
    property bool done: false

    Rectangle {
        id: card
        anchors.fill: parent
        radius: 36
        color: "#16181F"
        opacity: 0
        scale: 0.94
        // Fade the card as one image; per-item opacity would let overlapping
        // shapes (film over reel) show through each other.
        layer.enabled: true

        LogoMark {
            id: logo
            showBackground: false
            width: 240
            height: 240
            anchors.horizontalCenter: parent.horizontalCenter
            y: 46
            wind: -1
        }

        Text {
            id: wordmark
            anchors.horizontalCenter: parent.horizontalCenter
            y: logo.y + logo.height + 26
            text: "ELVIDEO"
            color: "#ECE8E1"
            font.family: "Segoe UI"
            font.pixelSize: 30
            font.weight: Font.DemiBold
            font.letterSpacing: 18
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
            // Card pops in.
            NumberAnimation { target: card; property: "opacity"; to: 1; duration: 350; easing.type: Easing.OutCubic }
            NumberAnimation { target: card; property: "scale"; to: 1; duration: 450; easing.type: Easing.OutBack }

            // Film winds down; the upper reel turns faster because it is smaller.
            // End angles are multiples of 120 deg so the holes land where the icon has them.
            NumberAnimation {
                target: logo; property: "topAngle"; from: 0; to: -840
                duration: splash.spinDuration; easing.type: Easing.InOutSine
            }
            NumberAnimation {
                target: logo; property: "bottomAngle"; from: 0; to: 600
                duration: splash.spinDuration; easing.type: Easing.InOutSine
            }
            NumberAnimation {
                target: logo; property: "wind"; from: -1; to: 0
                duration: splash.spinDuration; easing.type: Easing.InOutSine
            }

            // Wordmark fades in while its tracking tightens.
            SequentialAnimation {
                PauseAnimation { duration: 500 }
                ParallelAnimation {
                    NumberAnimation { target: wordmark; property: "opacity"; to: 1; duration: 700; easing.type: Easing.OutCubic }
                    NumberAnimation { target: wordmark; property: "font.letterSpacing"; to: 10; duration: 900; easing.type: Easing.OutCubic }
                }
            }

            // The record light blinks twice.
            SequentialAnimation {
                PauseAnimation { duration: 900 }
                loops: 1
                SequentialAnimation {
                    loops: 2
                    NumberAnimation { target: logo; property: "dotOpacity"; to: 0.25; duration: 250; easing.type: Easing.InOutQuad }
                    NumberAnimation { target: logo; property: "dotOpacity"; to: 1; duration: 250; easing.type: Easing.InOutQuad }
                }
            }
        }

        PauseAnimation { duration: 300 }

        ParallelAnimation {
            NumberAnimation { target: wordmark; property: "opacity"; to: 0; duration: 300; easing.type: Easing.InCubic }
            SequentialAnimation {
                PauseAnimation { duration: 120 }
                ParallelAnimation {
                    NumberAnimation { target: card; property: "opacity"; to: 0; duration: 380; easing.type: Easing.InCubic }
                    NumberAnimation { target: card; property: "scale"; to: 1.03; duration: 380; easing.type: Easing.InCubic }
                }
            }
        }

        ScriptAction { script: splash.finish() }
    }
}
