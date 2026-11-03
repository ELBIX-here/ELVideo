import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 980
    height: 660
    minimumWidth: 720
    minimumHeight: 520
    visible: false  // shown when the splash finishes
    title: qsTr("ELVideo")
    color: Theme.background

    // Palette for the Basic style controls that are not restyled (menus, tooltips, scroll bars).
    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.surfaceRaised
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.text
    palette.brightText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.accentText
    palette.light: Theme.surfaceHover
    palette.midlight: Theme.surfaceHover
    palette.mid: Theme.border
    palette.dark: Theme.textFaint
    palette.shadow: "#000000"
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.text
    palette.placeholderText: Theme.textFaint
    font.family: Theme.fontFamily

    property bool showSettings: false
    property bool showLibrary: false
    property bool hiddenForRecording: false
    property bool wasActive: false
    property bool trayHintShown: false
    property double lastRefresh: 0

    function showMain() {
        hiddenForRecording = false
        root.show()
        if (root.visibility === Window.Minimized) root.showNormal()
        root.raise()
        root.requestActivate()
    }

    function refreshSources() {
        lastRefresh = Date.now()
        sources.refresh()
    }

    Binding { target: Theme; property: "appearance"; value: appSettings.appearance }
    Connections {
        target: Theme
        function onDarkChanged() { desktop.useDarkTitleBar(root, Theme.dark) }
    }

    Component.onCompleted: {
        desktop.useDarkTitleBar(root, Theme.dark)
        desktop.excludeFromCapture(root)
    }

    onClosing: close => {
        if (appSettings.closeToTray) {
            close.accepted = false
            root.hide()
            if (!trayHintShown) {
                trayHintShown = true
                tray.notify("ELVideo", qsTr("트레이에서 계속 실행 중입니다. %1로 녹화를 시작할 수 있습니다.")
                                            .arg(appSettings.hotkeyStartStop))
            }
        } else {
            Qt.quit()
        }
    }

    // Thumbnails go stale while the window is in the background.
    onActiveChanged: if (active && !recorder.active && Date.now() - lastRefresh > 3000) refreshSources()

    Splash {
        id: splash
        onFinished: {
            root.showMain()
            splash.close()
            if (crashDumps.length > 0)
                toast.showWarning(qsTr("지난번에 ELVideo가 비정상 종료되었습니다. 녹화 중이었다면 fMP4 파일은 그때까지 재생됩니다. 문제 보고 시 덤프 파일을 첨부해 주세요."),
                                  crashDumpDir)
        }
    }

    FloatingBar {
        visible: recorder.active && appSettings.showFloatingBar
    }

    // Region recording: choose with the overlay, outline it while recording.
    property rect recordingRegion: Qt.rect(0, 0, 0, 0)
    RegionSelector {
        id: regionSelector
        onFinished: accepted => root.showMain()
    }
    ScreenshotOverlay {}

    // Delayed screenshot countdown; excluded from capture like the other overlays.
    Window {
        id: countdownWindow
        property int seconds: 0
        visible: seconds > 0
        width: 120
        height: 120
        x: Screen.virtualX + (Screen.width - width) / 2
        y: Screen.virtualY + (Screen.height - height) / 2
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowTransparentForInput
        color: "transparent"
        Component.onCompleted: desktop.excludeFromCapture(countdownWindow)
        Rectangle {
            anchors.fill: parent
            radius: 28
            color: Theme.hud
            Text {
                anchors.centerIn: parent
                text: countdownWindow.seconds
                color: Theme.hudText
                font.family: Theme.displayFont
                font.pixelSize: 60
                font.weight: Font.Light
                // A gentle pop on every tick.
                scale: 1
                onTextChanged: tick.restart()
                NumberAnimation on scale { id: tick; from: 1.25; to: 1; duration: 300; easing.type: Easing.OutCubic }
            }
        }
    }

    Connections {
        target: screenCapture
        function onCountdown(seconds) { countdownWindow.seconds = seconds }
        function onCaptured(path, width, height, copied) {
            const size = width + "×" + height
            const text = path.length === 0 ? qsTr("캡처를 클립보드에 복사했습니다 (%1).").arg(size)
                       : copied ? qsTr("캡처를 저장하고 클립보드에 복사했습니다 (%1).").arg(size)
                       : qsTr("캡처를 저장했습니다 (%1).").arg(size)
            toast.showSaved(text, path, qsTr("열기"))
        }
        function onFailed(message) { toast.showError(message) }
    }

    RegionFrame {
        region: root.recordingRegion
        visible: recorder.active && root.recordingRegion.width > 0
    }
    function selectRegion() {
        root.hide()
        regionSelector.open()
    }

    Connections {
        target: recorder
        function onStateChanged() {
            if (recorder.active === root.wasActive) return
            root.wasActive = recorder.active
            if (recorder.active) {
                root.showSettings = false
                root.recordingRegion = sources.regionMode && sources.hasRegion ? sources.regionGeometry
                                                                              : Qt.rect(0, 0, 0, 0)
                if (appSettings.hideWindowWhileRecording && root.visible) {
                    root.hiddenForRecording = true
                    root.hide()
                }
            } else if (root.hiddenForRecording) {
                root.showMain()
            }
        }
        function onFinished(path, reason, durationMs) {
            const text = reason === "sourceClosed" ? qsTr("창이 닫혀서 녹화를 마쳤습니다 (%1).")
                       : reason === "error" ? qsTr("오류로 녹화가 중단되었습니다. 그때까지의 영상은 저장했습니다 (%1).")
                       : qsTr("녹화를 저장했습니다 (%1).")
            toast.showSaved(text.arg(Theme.formatTime(durationMs)), path)
            if (!root.visible && !root.hiddenForRecording)
                tray.notify(qsTr("녹화 저장됨"), desktop.fileName(path))
        }
        function onReplaySaved(path, lengthMs) {
            toast.showSaved(qsTr("리플레이 %1초를 저장했습니다.").arg(Math.round(lengthMs / 1000)), path)
            if (!root.visible) tray.notify(qsTr("리플레이 저장됨"), desktop.fileName(path))
        }
        function onWarningOccurred(message) {
            toast.showWarning(qsTr("녹화는 계속됩니다: %1").arg(message))
            if (!root.visible) tray.notify(qsTr("오디오 경고"), message)
        }
        function onErrorOccurred(message) {
            toast.showError(message)
            if (!root.visible && !root.hiddenForRecording) tray.notify(qsTr("녹화 오류"), message)
        }
    }

    Connections {
        target: tray
        function onShowRequested() { root.showMain() }
    }
    Connections {
        target: hotkeys
        function onShowRequested() { root.showMain() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        anchors.topMargin: 16
        spacing: 16

        // Header (the "sub-nav"): app mark and the section name on the left, the
        // section's utility buttons on the right. Flat on the window background.
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            LogoMark {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }
            Text {
                text: recorder.replayMode ? qsTr("리플레이 버퍼") : recorder.active ? qsTr("녹화 중")
                    : root.showLibrary ? qsTr("라이브러리") : root.showSettings ? qsTr("설정") : qsTr("화면 녹화")
                color: Theme.text
                font.family: Theme.displayFont
                font.pixelSize: Theme.tagline
                font.weight: Font.DemiBold
                font.letterSpacing: Theme.tracking(Theme.tagline)
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: !captureSupported
                text: qsTr("이 시스템은 화면 캡처를 지원하지 않습니다")
                color: Theme.danger
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            IconButton {
                glyph: Theme.iconLibrary
                tip: qsTr("라이브러리")
                checkedLook: root.showLibrary
                onClicked: {
                    root.showLibrary = !root.showLibrary
                    root.showSettings = false
                }
            }
            IconButton {
                visible: desktop.hasPlayer()
                glyph: Theme.iconPlay
                tip: qsTr("동영상 열기 (ELVideo 플레이어)")
                onClicked: desktop.openInPlayer()
            }
            IconButton {
                glyph: Theme.iconSettings
                tip: root.showSettings ? qsTr("설정 닫기") : qsTr("설정")
                checkedLook: root.showSettings
                onClicked: {
                    root.showSettings = !root.showSettings
                    root.showLibrary = false
                }
            }
        }

        // Pages fill the rest and scroll under the floating record bar, which shows
        // them blurred through its frosted glass.
        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property real barInset: controlBar.height + 16

        StackLayout {
            id: pages
            anchors.fill: parent
            currentIndex: recorder.active ? 2 : root.showLibrary ? 3 : root.showSettings ? 1 : 0
            // Pages cross-fade in with a slight rise.
            onCurrentIndexChanged: pageIn.restart()
            ParallelAnimation {
                id: pageIn
                NumberAnimation { target: pages; property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic }
                NumberAnimation { target: pageShift; property: "y"; from: 8; to: 0; duration: Theme.normal; easing.type: Easing.OutCubic }
            }
            transform: Translate { id: pageShift }
            SourcePicker {
                bottomInset: body.barInset
                onRegionRequested: root.selectRegion()
            }
            SettingsPage { bottomInset: body.barInset }
            PreviewPanel { bottomInset: body.barInset }
            LibraryPage {
                bottomInset: body.barInset
                onMessage: text => toast.showSaved(text, "")
                onRecordRequested: root.showLibrary = false
            }
        }

        Toast {
            id: toast
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: controlBar.top
            anchors.bottomMargin: 10
        }

        // The record bar's frosted glass, next to the pages it blurs.
        Frost {
            x: controlBar.x
            y: controlBar.y
            width: controlBar.width
            height: controlBar.height
            source: pages
            radius: Theme.barRadius
        }
        ControlBar {
            id: controlBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }
        }
    }
}
