import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// ELVideo player window (player process). Keys follow PotPlayer where it has one.
// Always dark, with the controls on a floating HUD like the system player.
ApplicationWindow {
    id: win

    width: 1280
    height: 760
    minimumWidth: mini ? 240 : 480
    minimumHeight: mini ? 135 : 320
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
    palette.highlightedText: Theme.accentText
    palette.light: Theme.surfaceHover
    palette.mid: Theme.border
    palette.dark: Theme.textFaint
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.text

    readonly property bool fullscreen: visibility === Window.FullScreen
    property bool controlsShown: true
    property bool infoShown: false
    property bool onTop: false
    property bool mini: false          // PiP: small, frameless, always on top
    property rect normalGeometry
    property bool normalize: false     // audio: dynaudnorm
    property bool hflip: false
    property bool vflip: false

    function toggleMini() {
        if (!mini) {
            if (fullscreen) showNormal()
            normalGeometry = Qt.rect(x, y, width, height)
            const w = 480
            const h = player.videoWidth > 0 ? Math.round(w * player.videoHeight / player.videoWidth) : 270
            mini = true
            flags = Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
            width = w
            height = h
            x = Screen.virtualX + Screen.desktopAvailableWidth - w - 24
            y = Screen.virtualY + Screen.desktopAvailableHeight - h - 24
        } else {
            mini = false
            flags = Qt.Window | (onTop ? Qt.WindowStaysOnTopHint : 0)
            x = normalGeometry.x
            y = normalGeometry.y
            width = normalGeometry.width
            height = normalGeometry.height
            desktop.useDarkTitleBar(win)
        }
    }
    function adjust(property, delta) {
        if (delta === 0) {
            for (const p of ["brightness", "contrast", "saturation", "gamma"]) player.setMpvProperty(p, 0)
            player.setMpvProperty("video-zoom", 0)
            player.setMpvProperty("video-pan-x", 0)
            player.setMpvProperty("video-pan-y", 0)
            osd(qsTr("화면 조정 초기화"))
            return
        }
        player.command(["add", property, delta])
        const names = { brightness: qsTr("밝기"), contrast: qsTr("대비"), saturation: qsTr("채도"), "video-zoom": qsTr("확대") }
        Qt.callLater(() => osd(names[property] + " " + Number(player.getMpvProperty(property)).toFixed(property === "video-zoom" ? 1 : 0)))
    }
    function setAspect(value, label) {
        player.setMpvProperty("video-aspect-override", value)
        osd(qsTr("화면 비율: %1").arg(label))
    }
    function rotate() {
        const r = (Number(player.getMpvProperty("video-rotate")) + 90) % 360
        player.setMpvProperty("video-rotate", r)
        osd(qsTr("회전 %1°").arg(r))
    }
    function flip(horizontal) {
        player.command(["vf", "toggle", horizontal ? "hflip" : "vflip"])
        if (horizontal) hflip = !hflip; else vflip = !vflip
        osd(horizontal ? (hflip ? qsTr("좌우 반전") : qsTr("좌우 반전 해제")) : (vflip ? qsTr("상하 반전") : qsTr("상하 반전 해제")))
    }
    function toggleNormalize() {
        player.command(["af", "toggle", "dynaudnorm"])
        normalize = !normalize
        osd(normalize ? qsTr("음량 평준화 켬") : qsTr("음량 평준화 끔"))
    }
    function chapterStep(delta) {
        if (player.chapters.length === 0) return
        player.command(["add", "chapter", delta])
        Qt.callLater(() => {
            const c = player.chapters[Number(player.getMpvProperty("chapter"))]
            if (c) osd(qsTr("챕터: %1").arg(c.title || seekBar.format(c.time)))
        })
    }

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

    Binding { target: Theme; property: "appearance"; value: "dark" }

    Component.onCompleted: {
        desktop.useDarkTitleBar(win)
        if (initialFile.length > 0) player.open(initialFile)
    }
    // Closing while mpv still has its audio output open crashes in the Qt Windows plugin
    // during shutdown. So: remember the position, stop (which releases the audio
    // device), and close for real once mpv is idle.
    property bool closeWhenIdle: false
    onClosing: close => {
        if (player.idle) return
        close.accepted = false
        if (closeWhenIdle) return
        closeWhenIdle = true
        player.saveResumePosition()
        player.command(["stop"])
        stopTimeout.start()
    }
    Connections {
        target: player
        function onIdleChanged() { if (win.closeWhenIdle && player.idle) Qt.callLater(win.close) }
    }
    Timer {  // never get stuck open if mpv does not report idle
        id: stopTimeout
        interval: 2000
        onTriggered: { player.command(["set_property", "options/ao", "null"]); Qt.callLater(Qt.quit) }
    }

    MpvItem {
        id: player
        anchors.fill: parent
        onError: message => win.osd(qsTr("재생할 수 없습니다: %1").arg(message))
        onFileLoaded: win.showControls()
        // Hover previews: the thumbnailer needs the file and the picture's aspect ratio.
        onVideoSizeChanged: if (videoWidth > 0) seekThumbs.setFile(path, videoWidth, videoHeight)
        onFileChanged: if (path.length === 0) seekThumbs.setFile("", 0, 0)
    }

    // Hides the controls 2.5 s after the last mouse move. While something keeps
    // them up (paused, nothing loaded, mouse on the bar, a menu open) it checks
    // again later instead of giving up, so they still go away afterwards.
    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: {
            if (player.paused || player.idle || controlsHover.hovered || win.menuOpen) restart()
            else win.controlsShown = false
        }
    }
    readonly property bool menuOpen: speedMenu.opened || subMenu.opened || audioMenu.opened || chapterMenu.opened
                                     || pictureMenu.opened || contextMenu.opened
    Connections {
        target: player
        function onPausedChanged() { if (!player.paused) hideTimer.restart() }
    }

    // Mouse on the video: click pauses, double-click toggles full screen, wheel = volume.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: win.controlsShown || !win.fullscreen ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: win.showControls()
        // Leaving the window (to another monitor, say) hides them soon too.
        onExited: hideTimer.restart()
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
            font.family: Theme.fontFamily
            font.pixelSize: 15
        }
        AppButton {
            anchors.horizontalCenter: parent.horizontalCenter
            primary: true
            text: qsTr("파일 열기…")
            onClicked: openDialog.open()
        }
    }

    // On-screen messages.
    Rectangle {
        id: osdBox
        anchors.horizontalCenter: parent.horizontalCenter
        y: 24
        width: osdText.implicitWidth + 36
        height: 38
        radius: 19
        color: Theme.hud
        border.color: Qt.rgba(1, 1, 1, 0.08)
        opacity: 0
        scale: 0.96 + 0.04 * opacity
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Text {
            id: osdText
            anchors.centerIn: parent
            color: Theme.hudText
            font.family: Theme.fontFamily
            font.pixelSize: 15
            font.weight: Font.DemiBold
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
        width: info.implicitWidth + 32
        height: info.implicitHeight + 24
        radius: 14
        color: Theme.hud
        border.color: Qt.rgba(1, 1, 1, 0.08)
        Text {
            id: info
            anchors.centerIn: parent
            color: Theme.hudText
            font.family: Theme.fontFamily
            font.pixelSize: 12
            lineHeight: 1.3
            text: [player.path,
                   qsTr("영상 %1×%2, 디코딩 %3").arg(player.videoWidth).arg(player.videoHeight)
                       .arg(player.hwdec.length > 0 && player.hwdec !== "no" ? player.hwdec : qsTr("소프트웨어")),
                   qsTr("배속 %1x · 자막 싱크 %2초 · 오디오 싱크 %3초").arg(player.speed.toFixed(2))
                       .arg(player.subDelay.toFixed(2)).arg(player.audioDelay.toFixed(2)),
                   qsTr("재생목록 %1 / %2").arg(player.playlistPos + 1).arg(player.playlistCount)].join("\n")
        }
    }

    // Frosted glass over the video under the controls, as in the system player.
    Frost {
        x: controls.x
        y: controls.y + controlsShift.y
        width: controls.width
        height: controls.height
        visible: controls.visible
        opacity: controls.opacity
        source: player
        radius: controls.radius
        tint: Qt.rgba(0.11, 0.11, 0.12, 0.55)
        base: "black"
        borderColor: Qt.rgba(1, 1, 1, 0.10)
    }

    // Controls.
    Rectangle {
        id: controls
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: win.fullscreen ? 28 : 16
        width: Math.min(parent.width - 32, 980)
        height: 86
        radius: Theme.barRadius
        color: "transparent"
        opacity: !win.mini && (win.controlsShown || player.paused || player.idle) ? 1 : 0
        visible: opacity > 0
        transform: Translate { id: controlsShift; y: controls.opacity < 1 ? 8 * (1 - controls.opacity) : 0 }
        Behavior on opacity { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
        // No layer on this item: the seek preview pops out above the bar.
        HoverHandler { id: controlsHover }
        // Lift subtitles above the controls while they show (mpv units: 720-line scale).
        function liftSubtitles() {
            player.setMpvProperty("sub-margin-y", visible ? Math.round((height + anchors.bottomMargin + 8) * 720 / win.height) : 22)
        }
        onVisibleChanged: liftSubtitles()
        Connections {
            target: win
            function onHeightChanged() { controls.liftSubtitles() }
        }
        Connections {
            target: player
            function onFileLoaded() { controls.liftSubtitles() }
        }

        SeekBar {
            id: seekBar
            player: player
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            y: 8
        }

        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 8
            spacing: 2

            IconButton {
                onHud: true
                glyph: player.paused || player.eof ? Theme.iconPlay : Theme.iconPause
                tip: player.paused ? qsTr("재생 (Space)") : qsTr("일시정지 (Space)")
                onClicked: player.eof ? player.seek(0) : player.togglePause()
            }
            IconButton {
                onHud: true
                glyph: ""  // Previous
                tip: qsTr("이전 파일 (PgUp)")
                enabled: player.playlistPos > 0
                onClicked: player.playlistStep(-1)
            }
            IconButton {
                onHud: true
                glyph: ""  // Next
                tip: qsTr("다음 파일 (PgDn)")
                enabled: player.playlistPos >= 0 && player.playlistPos + 1 < player.playlistCount
                onClicked: player.playlistStep(1)
            }
            IconButton {
                onHud: true
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
                    color: Qt.rgba(1, 1, 1, 0.22)
                    Rectangle {
                        width: volumeSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: volumeSlider.value > 100 ? Theme.warning : Theme.hudText  // above 100 % is boost
                    }
                }
                handle: Rectangle {
                    x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: "white"
                    scale: volumeSlider.pressed ? 1.15 : volumeSlider.hovered ? 1 : 0.85
                    Behavior on scale { NumberAnimation { duration: Theme.fast } }
                }
            }
            Text {
                Layout.leftMargin: 8
                text: seekBar.format(player.position) + " / " + seekBar.format(player.duration)
                color: Theme.hudTextDim
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.features: { "tnum": 1 }
            }
            Item { Layout.fillWidth: true }

            IconButton {
                onHud: true
                visible: player.chapters.length > 0
                glyph: "\uE8FD"  // BulletedList
                tip: qsTr("챕터 (Shift+PgUp / PgDn)")
                onClicked: chapterMenu.popup()
            }
            HudButton {
                text: Number(player.speed.toFixed(2)) + "×"
                onClicked: speedMenu.popup()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("배속 (C 빠르게, X 느리게, Z 원래대로)")
                AppMenu {
                    id: speedMenu
                    Repeater {
                        model: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 3, 4]
                        AppMenuItem {
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
                onHud: true
                glyph: ""  // CC
                tip: qsTr("자막")
                enabled: !player.idle
                onClicked: subMenu.popup()
            }
            IconButton {
                onHud: true
                glyph: ""  // Audio
                tip: qsTr("오디오 트랙")
                enabled: !player.idle
                onClicked: audioMenu.popup()
            }
            IconButton {
                onHud: true
                glyph: ""  // RepeatAll
                tip: qsTr("구간 반복 A-B (L)")
                checkedLook: player.loopA >= 0
                enabled: !player.idle
                onClicked: win.abLoop()
            }
            IconButton {
                onHud: true
                glyph: Theme.iconCamera
                tip: qsTr("현재 장면 저장 (S)")
                enabled: !player.idle
                onClicked: win.takeScreenshot()
            }
            IconButton {
                onHud: true
                glyph: ""  // Pin
                tip: qsTr("항상 위 (Ctrl+T)")
                checkedLook: win.onTop
                onClicked: win.onTop = !win.onTop
            }
            IconButton {
                onHud: true
                glyph: "\uE8A7"  // OpenInNewWindow
                tip: qsTr("미니 플레이어 (P)")
                enabled: !player.idle
                onClicked: win.toggleMini()
            }
            IconButton {
                onHud: true
                glyph: win.fullscreen ? "" : ""
                tip: qsTr("전체 화면 (Enter, 더블클릭)")
                onClicked: win.toggleFullscreen()
            }
        }
    }
    onOnTopChanged: if (!mini) flags = onTop ? (flags | Qt.WindowStaysOnTopHint) : (flags & ~Qt.WindowStaysOnTopHint)

    // Mini player: drag strip and a few buttons on hover.
    Item {
        anchors.fill: parent
        visible: win.mini
        HoverHandler { id: miniHover }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 32
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.6) }
                GradientStop { position: 1; color: "transparent" }
            }
            opacity: miniHover.hovered ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
            DragHandler {
                target: null
                onActiveChanged: if (active) win.startSystemMove()
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                IconButton {
                    onHud: true
                    width: 30
                    height: 30
                    iconSize: 12
                    glyph: player.paused ? Theme.iconPlay : Theme.iconPause
                    tip: player.paused ? qsTr("재생") : qsTr("일시정지")
                    onClicked: player.togglePause()
                }
                IconButton {
                    onHud: true
                    width: 30
                    height: 30
                    iconSize: 12
                    glyph: "\uE73F"  // BackToWindow
                    tip: qsTr("원래 크기로 (P)")
                    onClicked: win.toggleMini()
                }
                IconButton {
                    onHud: true
                    width: 30
                    height: 30
                    iconSize: 12
                    glyph: Theme.iconClose
                    tip: qsTr("닫기")
                    onClicked: win.close()
                }
            }
        }
        Rectangle {  // thin progress line
            anchors.bottom: parent.bottom
            height: 3
            width: player.duration > 0 ? parent.width * player.position / player.duration : 0
            color: Theme.accent
        }
    }

    // Track menus, rebuilt from mpv's track-list.
    component TrackMenu: AppMenu {
        id: menu
        property string type
        Instantiator {
            model: player.tracks.filter(t => t.type === menu.type)
            delegate: AppMenuItem {
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
        AppMenuItem { text: qsTr("자막 끄기"); onTriggered: player.selectTrack("sub", 0) }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("자막 파일 추가…"); onTriggered: subtitleDialog.open() }
        AppMenuItem { text: qsTr("자막 0.1초 빠르게 (<)"); onTriggered: { player.addSubDelay(-0.1); win.osd(qsTr("자막 싱크 %1초").arg((player.subDelay - 0.1).toFixed(1))) } }
        AppMenuItem { text: qsTr("자막 0.1초 느리게 (>)"); onTriggered: { player.addSubDelay(0.1); win.osd(qsTr("자막 싱크 %1초").arg((player.subDelay + 0.1).toFixed(1))) } }
    }
    TrackMenu {
        id: audioMenu
        type: "audio"
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("오디오 0.05초 빠르게 ([)"); onTriggered: player.addAudioDelay(-0.05) }
        AppMenuItem { text: qsTr("오디오 0.05초 느리게 (])"); onTriggered: player.addAudioDelay(0.05) }
    }

    AppMenu {
        id: chapterMenu
        Instantiator {
            model: player.chapters
            delegate: AppMenuItem {
                required property var modelData
                required property int index
                text: seekBar.format(modelData.time) + "   " + (modelData.title || qsTr("챕터 %1").arg(index + 1))
                onTriggered: player.seek(modelData.time)
            }
            onObjectAdded: (index, object) => chapterMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => chapterMenu.removeItem(object)
        }
    }
    AppMenu {
        id: pictureMenu
        title: qsTr("화면")
        AppMenu {
            title: qsTr("화면 비율")
            AppMenuItem { text: qsTr("원본"); onTriggered: win.setAspect("-1", qsTr("원본")) }
            AppMenuItem { text: "16:9"; onTriggered: win.setAspect("16:9", "16:9") }
            AppMenuItem { text: "4:3"; onTriggered: win.setAspect("4:3", "4:3") }
            AppMenuItem { text: "21:9 (2.35:1)"; onTriggered: win.setAspect("2.35:1", "2.35:1") }
            AppMenuItem { text: "1:1"; onTriggered: win.setAspect("1:1", "1:1") }
        }
        AppMenuItem { text: qsTr("확대 (Ctrl+=)"); onTriggered: win.adjust("video-zoom", 0.1) }
        AppMenuItem { text: qsTr("축소 (Ctrl+-)"); onTriggered: win.adjust("video-zoom", -0.1) }
        AppMenuItem { text: qsTr("90° 회전 (Ctrl+R)"); onTriggered: win.rotate() }
        AppMenuItem { text: qsTr("좌우 반전"); checkable: true; checked: win.hflip; onTriggered: win.flip(true) }
        AppMenuItem { text: qsTr("상하 반전"); checkable: true; checked: win.vflip; onTriggered: win.flip(false) }
        AppMenuSeparator {}
        AppMenuItem { text: qsTr("밝기 + / − (W / Q)"); onTriggered: win.adjust("brightness", 5) }
        AppMenuItem { text: qsTr("대비 + / − (R / E)"); onTriggered: win.adjust("contrast", 5) }
        AppMenuItem { text: qsTr("채도 + / − (Y / T)"); onTriggered: win.adjust("saturation", 5) }
        AppMenuItem { text: qsTr("화면 조정 초기화 (U)"); onTriggered: win.adjust("", 0) }
    }

    AppMenu {
        id: contextMenu
        AppMenuItem { text: qsTr("파일 열기… (Ctrl+O)"); onTriggered: openDialog.open() }
        AppMenuItem { text: qsTr("자막 파일 추가…"); enabled: !player.idle; onTriggered: subtitleDialog.open() }
        AppMenuSeparator {}
        AppMenuItem { text: player.paused ? qsTr("재생") : qsTr("일시정지"); enabled: !player.idle; onTriggered: player.togglePause() }
        AppMenuItem { text: qsTr("구간 반복 A-B (L)"); enabled: !player.idle; onTriggered: win.abLoop() }
        AppMenuItem { text: qsTr("구간 반복 해제"); enabled: player.loopA >= 0; onTriggered: player.clearAbLoop() }
        AppMenuItem { text: qsTr("현재 장면 저장 (S)"); enabled: !player.idle; onTriggered: win.takeScreenshot() }
        AppMenuSeparator {}
        AppMenu {
            title: qsTr("챕터")
            enabled: player.chapters.length > 0
            AppMenuItem { text: qsTr("다음 챕터 (Shift+PgDn)"); onTriggered: win.chapterStep(1) }
            AppMenuItem { text: qsTr("이전 챕터 (Shift+PgUp)"); onTriggered: win.chapterStep(-1) }
        }
        AppMenuItem { text: qsTr("화면…"); enabled: !player.idle; onTriggered: pictureMenu.popup() }
        AppMenuItem { text: qsTr("음량 평준화 (N)"); checkable: true; checked: win.normalize; enabled: !player.idle; onTriggered: win.toggleNormalize() }
        AppMenuItem { text: qsTr("미니 플레이어 (P)"); checkable: true; checked: win.mini; enabled: !player.idle; onTriggered: win.toggleMini() }
        AppMenuItem { text: qsTr("미디어 정보 (I)"); checkable: true; checked: win.infoShown; onTriggered: win.infoShown = !win.infoShown }
        AppMenuItem { text: qsTr("항상 위 (Ctrl+T)"); checkable: true; checked: win.onTop; onTriggered: win.onTop = !win.onTop }
        AppMenuItem { text: qsTr("전체 화면 (Enter)"); onTriggered: win.toggleFullscreen() }
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
    Shortcut { sequence: "P"; enabled: !player.idle; onActivated: win.toggleMini() }
    Shortcut { sequence: "N"; enabled: !player.idle; onActivated: win.toggleNormalize() }
    Shortcut { sequence: "Shift+PgDown"; onActivated: win.chapterStep(1) }
    Shortcut { sequence: "Shift+PgUp"; onActivated: win.chapterStep(-1) }
    Shortcut { sequence: "Ctrl+R"; onActivated: win.rotate() }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++"]; onActivated: win.adjust("video-zoom", 0.1) }
    Shortcut { sequence: "Ctrl+-"; onActivated: win.adjust("video-zoom", -0.1) }
    Shortcut { sequence: "Q"; onActivated: win.adjust("brightness", -5) }
    Shortcut { sequence: "W"; onActivated: win.adjust("brightness", 5) }
    Shortcut { sequence: "E"; onActivated: win.adjust("contrast", -5) }
    Shortcut { sequence: "R"; onActivated: win.adjust("contrast", 5) }
    Shortcut { sequence: "T"; onActivated: win.adjust("saturation", -5) }
    Shortcut { sequence: "Y"; onActivated: win.adjust("saturation", 5) }
    Shortcut { sequence: "U"; onActivated: win.adjust("", 0) }
}
