import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// ELVideo player window (player process). Keys follow PotPlayer where it has one.
ApplicationWindow {
    id: win

    width: 1280
    height: 760
    minimumWidth: 480
    minimumHeight: 320
    visible: true
    color: "black"
    title: player.idle || player.title.length === 0 ? qsTr("ELVideo 플레이어") : player.title + " — ELVideo"
    font.family: Theme.fontFamily

    palette.window: Theme.surface
    palette.windowText: Theme.text
    palette.base: Theme.surfaceRaised
    palette.text: Theme.text
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.background
    palette.light: Theme.surfaceHover
    palette.mid: Theme.border
    palette.dark: Theme.textFaint
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.text

    readonly property bool fullscreen: visibility === Window.FullScreen
    property bool controlsShown: true
    property bool infoShown: false
    property bool onTop: false

    function showControls() {
        controlsShown = true
        hideTimer.restart()
    }
    function osd(text) {
        osdText.text = text
        osdBox.opacity = 1
        osdTimer.restart()
    }
    function toggleFullscreen() { fullscreen ? showNormal() : showFullScreen() }
    function seekBy(seconds) {
        player.seek(seconds, true)
        osd((seconds > 0 ? "+" : "") + seconds + qsTr("초"))
    }
    function changeVolume(delta) {
        player.volume = Math.max(0, Math.min(150, Math.round(player.volume + delta)))
        osd(qsTr("볼륨 %1%").arg(Math.round(player.volume)))
    }
    function changeSpeed(delta) {
        player.speed = delta === 0 ? 1 : Math.max(0.25, Math.min(4, Math.round((player.speed + delta) * 100) / 100))
        osd(qsTr("배속 %1x").arg(player.speed.toFixed(2)))
    }
    function takeScreenshot() {
        const file = player.screenshot(appSettings.screenshotDir || appSettings.defaultScreenshotDir)
        osd(file.length > 0 ? qsTr("스크린샷 저장: %1").arg(desktop.fileName(file)) : qsTr("스크린샷을 저장하지 못했습니다"))
    }
    function abLoop() {
        player.cycleAbLoop()
        Qt.callLater(() => osd(player.loopA < 0 ? qsTr("구간 반복 해제")
                             : player.loopB < 0 ? qsTr("구간 반복 시작점 %1").arg(seekBar.format(player.loopA))
                             : qsTr("구간 반복 %1 – %2").arg(seekBar.format(player.loopA)).arg(seekBar.format(player.loopB))))
    }
    function isSubtitle(url) { return /\.(srt|ass|ssa|smi|sami|vtt|sub|sup)$/i.test(url) }

    Component.onCompleted: {
        desktop.useDarkTitleBar(win)
        if (initialFile.length > 0) player.open(initialFile)
    }
    onClosing: player.saveResumePosition()

    MpvItem {
        id: player
        anchors.fill: parent
        onError: message => win.osd(qsTr("재생할 수 없습니다: %1").arg(message))
        onFileLoaded: win.showControls()
    }

    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: if (!player.paused && !player.idle && !controlsHover.hovered) win.controlsShown = false
    }

    // Mouse on the video: click pauses, double-click toggles full screen, wheel = volume.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: win.controlsShown || !win.fullscreen ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: win.showControls()
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) contextMenu.popup()
            else if (!player.idle) player.togglePause()
        }
        onDoubleClicked: mouse => {
            if (mouse.button !== Qt.LeftButton) return
            if (!player.idle) player.togglePause()  // undo the click's pause
            win.toggleFullscreen()
        }
        onWheel: wheel => win.changeVolume(wheel.angleDelta.y > 0 ? 5 : -5)
    }

    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            if (!drop.hasUrls) return
            const url = drop.urls[0].toString()
            if (win.isSubtitle(url) && !player.idle) {
                player.addSubtitle(url)
                win.osd(qsTr("자막 추가: %1").arg(desktop.fileName(desktop.toLocalFile(drop.urls[0]))))
            } else {
                player.open(url)
            }
        }
    }

    // Nothing loaded yet.
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: player.idle
        LogoMark {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 96
            height: 96
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("동영상을 끌어다 놓거나 여세요")
            color: Theme.textDim
            font.pixelSize: 15
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("파일 열기…")
            onClicked: openDialog.open()
        }
    }

    // On-screen messages.
    Rectangle {
        id: osdBox
        x: 20
        y: 20
        width: osdText.implicitWidth + 24
        height: 36
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.6)
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Text {
            id: osdText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 16
        }
        Timer {
            id: osdTimer
            interval: 1600
            onTriggered: osdBox.opacity = 0
        }
    }

    // Media information (I).
    Rectangle {
        visible: win.infoShown && !player.idle
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20
        width: info.implicitWidth + 24
        height: info.implicitHeight + 20
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.7)
        Text {
            id: info
            anchors.centerIn: parent
            color: "white"
            font.family: Theme.monoFont
            font.pixelSize: 12
            text: [player.path,
                   qsTr("영상 %1×%2, 디코딩 %3").arg(player.videoWidth).arg(player.videoHeight)
                       .arg(player.hwdec.length > 0 && player.hwdec !== "no" ? player.hwdec : qsTr("소프트웨어")),
                   qsTr("배속 %1x · 자막 싱크 %2초 · 오디오 싱크 %3초").arg(player.speed.toFixed(2))
                       .arg(player.subDelay.toFixed(2)).arg(player.audioDelay.toFixed(2)),
                   qsTr("재생목록 %1 / %2").arg(player.playlistPos + 1).arg(player.playlistCount)].join("\n")
        }
    }

    // Controls.
    Rectangle {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 96
        opacity: win.controlsShown || player.paused || player.idle ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.85) }
        }
        HoverHandler { id: controlsHover }
        // Lift subtitles above the controls while they show (mpv units: 720-line scale).
        onVisibleChanged: player.setMpvProperty("sub-margin-y", visible ? Math.round((height + 8) * 720 / win.height) : 22)

        SeekBar {
            id: seekBar
            player: player
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            y: 30
        }

        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 6
            spacing: 2

            IconButton {
                glyph: player.paused || player.eof ? Theme.iconPlay : Theme.iconPause
                tip: player.paused ? qsTr("재생 (Space)") : qsTr("일시정지 (Space)")
                onClicked: player.eof ? player.seek(0) : player.togglePause()
            }
            IconButton {
                glyph: ""  // Previous
                tip: qsTr("이전 파일 (PgUp)")
                enabled: player.playlistPos > 0
                onClicked: player.playlistStep(-1)
            }
            IconButton {
                glyph: ""  // Next
                tip: qsTr("다음 파일 (PgDn)")
                enabled: player.playlistPos >= 0 && player.playlistPos + 1 < player.playlistCount
                onClicked: player.playlistStep(1)
            }
            IconButton {
                glyph: player.muted || player.volume === 0 ? "" : Theme.iconVolume
                tip: qsTr("음소거 (M)")
                onClicked: player.muted = !player.muted
            }
            Slider {
                id: volumeSlider
                Layout.preferredWidth: 110
                from: 0
                to: 150
                value: player.volume
                onMoved: player.volume = value
                ToolTip.visible: hovered || pressed
                ToolTip.text: Math.round(value) + "%"
                background: Rectangle {
                    x: volumeSlider.leftPadding
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: volumeSlider.availableWidth
                    height: 4
                    radius: 2
                    color: Qt.rgba(1, 1, 1, 0.25)
                    Rectangle {
                        width: volumeSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: volumeSlider.value > 100 ? Theme.accent : Theme.text  // above 100 % is boost
                    }
                }
                handle: Rectangle {
                    x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: Theme.text
                }
            }
            Text {
                Layout.leftMargin: 8
                text: seekBar.format(player.position) + " / " + seekBar.format(player.duration)
                color: Theme.text
                font.family: Theme.monoFont
                font.pixelSize: 13
            }
            Item { Layout.fillWidth: true }

            ToolButton {
                text: player.speed.toFixed(2).replace(/0$/, "") + "x"
                font.pixelSize: 13
                onClicked: speedMenu.popup()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("배속 (C 빠르게, X 느리게, Z 원래대로)")
                Menu {
                    id: speedMenu
                    Repeater {
                        model: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 3, 4]
                        MenuItem {
                            required property var modelData
                            text: modelData + "x"
                            checkable: true
                            checked: Math.abs(player.speed - modelData) < 0.001
                            onTriggered: player.speed = modelData
                        }
                    }
                }
            }
            IconButton {
                glyph: ""  // CC
                tip: qsTr("자막")
                enabled: !player.idle
                onClicked: subMenu.popup()
            }
            IconButton {
                glyph: ""  // Audio
                tip: qsTr("오디오 트랙")
                enabled: !player.idle
                onClicked: audioMenu.popup()
            }
            IconButton {
                glyph: ""  // RepeatAll
                tip: qsTr("구간 반복 A-B (L)")
                iconColor: player.loopA >= 0 ? Theme.accent : Theme.text
                enabled: !player.idle
                onClicked: win.abLoop()
            }
            IconButton {
                glyph: Theme.iconCamera
                tip: qsTr("현재 장면 저장 (S)")
                enabled: !player.idle
                onClicked: win.takeScreenshot()
            }
            IconButton {
                glyph: ""  // Pin
                tip: qsTr("항상 위 (Ctrl+T)")
                iconColor: win.onTop ? Theme.accent : Theme.text
                onClicked: win.onTop = !win.onTop
            }
            IconButton {
                glyph: win.fullscreen ? "" : ""
                tip: qsTr("전체 화면 (Enter, 더블클릭)")
                onClicked: win.toggleFullscreen()
            }
        }
    }
    onOnTopChanged: flags = onTop ? (flags | Qt.WindowStaysOnTopHint) : (flags & ~Qt.WindowStaysOnTopHint)

    // Track menus, rebuilt from mpv's track-list.
    component TrackMenu: Menu {
        id: menu
        property string type
        Instantiator {
            model: player.tracks.filter(t => t.type === menu.type)
            delegate: MenuItem {
                required property var modelData
                text: (modelData.title || modelData.lang || (qsTr("트랙 ") + modelData.id))
                      + (modelData.lang && modelData.title ? " [" + modelData.lang + "]" : "")
                      + (modelData.external ? qsTr(" (외부)") : "")
                checkable: true
                checked: modelData.selected === true
                onTriggered: player.selectTrack(menu.type, modelData.id)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }
    TrackMenu {
        id: subMenu
        type: "sub"
        MenuItem { text: qsTr("자막 끄기"); onTriggered: player.selectTrack("sub", 0) }
        MenuSeparator {}
        MenuItem { text: qsTr("자막 파일 추가…"); onTriggered: subtitleDialog.open() }
        MenuItem { text: qsTr("자막 0.1초 빠르게 (<)"); onTriggered: { player.addSubDelay(-0.1); win.osd(qsTr("자막 싱크 %1초").arg((player.subDelay - 0.1).toFixed(1))) } }
        MenuItem { text: qsTr("자막 0.1초 느리게 (>)"); onTriggered: { player.addSubDelay(0.1); win.osd(qsTr("자막 싱크 %1초").arg((player.subDelay + 0.1).toFixed(1))) } }
    }
    TrackMenu {
        id: audioMenu
        type: "audio"
        MenuSeparator {}
        MenuItem { text: qsTr("오디오 0.05초 빠르게 ([)"); onTriggered: player.addAudioDelay(-0.05) }
        MenuItem { text: qsTr("오디오 0.05초 느리게 (])"); onTriggered: player.addAudioDelay(0.05) }
    }

    Menu {
        id: contextMenu
        MenuItem { text: qsTr("파일 열기… (Ctrl+O)"); onTriggered: openDialog.open() }
        MenuItem { text: qsTr("자막 파일 추가…"); enabled: !player.idle; onTriggered: subtitleDialog.open() }
        MenuSeparator {}
        MenuItem { text: player.paused ? qsTr("재생") : qsTr("일시정지"); enabled: !player.idle; onTriggered: player.togglePause() }
        MenuItem { text: qsTr("구간 반복 A-B (L)"); enabled: !player.idle; onTriggered: win.abLoop() }
        MenuItem { text: qsTr("구간 반복 해제"); enabled: player.loopA >= 0; onTriggered: player.clearAbLoop() }
        MenuItem { text: qsTr("현재 장면 저장 (S)"); enabled: !player.idle; onTriggered: win.takeScreenshot() }
        MenuSeparator {}
        MenuItem { text: qsTr("미디어 정보 (I)"); checkable: true; checked: win.infoShown; onTriggered: win.infoShown = !win.infoShown }
        MenuItem { text: qsTr("항상 위 (Ctrl+T)"); checkable: true; checked: win.onTop; onTriggered: win.onTop = !win.onTop }
        MenuItem { text: qsTr("전체 화면 (Enter)"); onTriggered: win.toggleFullscreen() }
    }

    FileDialog {
        id: openDialog
        title: qsTr("동영상 열기")
        nameFilters: [qsTr("동영상 (*.mp4 *.mkv *.avi *.mov *.wmv *.webm *.ts *.m2ts *.flv *.m4v)"), qsTr("모든 파일 (*)")]
        onAccepted: player.open(selectedFile.toString())
    }
    FileDialog {
        id: subtitleDialog
        title: qsTr("자막 파일 추가")
        nameFilters: [qsTr("자막 (*.srt *.ass *.ssa *.smi *.vtt *.sub)"), qsTr("모든 파일 (*)")]
        onAccepted: player.addSubtitle(selectedFile.toString())
    }

    // Keyboard.
    Shortcut { sequence: "Space"; onActivated: { player.togglePause(); win.osd(player.paused ? qsTr("재생") : qsTr("일시정지")) } }
    Shortcut { sequence: "Left"; onActivated: win.seekBy(-5) }
    Shortcut { sequence: "Right"; onActivated: win.seekBy(5) }
    Shortcut { sequence: "Ctrl+Left"; onActivated: win.seekBy(-30) }
    Shortcut { sequence: "Ctrl+Right"; onActivated: win.seekBy(30) }
    Shortcut { sequence: "Home"; onActivated: player.seek(0) }
    Shortcut { sequence: "Up"; onActivated: win.changeVolume(5) }
    Shortcut { sequence: "Down"; onActivated: win.changeVolume(-5) }
    Shortcut { sequence: "M"; onActivated: { player.muted = !player.muted; win.osd(player.muted ? qsTr("음소거 해제") : qsTr("음소거")) } }
    Shortcut { sequences: ["Return", "Enter"]; onActivated: win.toggleFullscreen() }
    Shortcut { sequence: "Esc"; enabled: win.fullscreen; onActivated: win.showNormal() }
    Shortcut { sequences: [".", "F"]; onActivated: player.frameStep(false) }
    Shortcut { sequences: [",", "D"]; onActivated: player.frameStep(true) }
    Shortcut { sequence: "C"; onActivated: win.changeSpeed(0.1) }
    Shortcut { sequence: "X"; onActivated: win.changeSpeed(-0.1) }
    Shortcut { sequence: "Z"; onActivated: win.changeSpeed(0) }
    Shortcut { sequence: "L"; onActivated: win.abLoop() }
    Shortcut { sequence: "S"; onActivated: win.takeScreenshot() }
    Shortcut { sequence: "I"; onActivated: win.infoShown = !win.infoShown }
    Shortcut { sequence: "<"; onActivated: { player.addSubDelay(-0.1); win.osd(qsTr("자막 싱크 %1초").arg((player.subDelay - 0.1).toFixed(1))) } }
    Shortcut { sequence: ">"; onActivated: { player.addSubDelay(0.1); win.osd(qsTr("자막 싱크 %1초").arg((player.subDelay + 0.1).toFixed(1))) } }
    Shortcut { sequence: "["; onActivated: player.addAudioDelay(-0.05) }
    Shortcut { sequence: "]"; onActivated: player.addAudioDelay(0.05) }
    Shortcut { sequence: "PgUp"; onActivated: player.playlistStep(-1) }
    Shortcut { sequence: "PgDown"; onActivated: player.playlistStep(1) }
    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+T"; onActivated: win.onTop = !win.onTop }
}
