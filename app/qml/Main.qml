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

    // Dark palette for the Basic style controls.
    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.surfaceRaised
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.text
    palette.brightText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.background
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

    Component.onCompleted: {
        desktop.useDarkTitleBar(root)
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

    Connections {
        target: recorder
        function onStateChanged() {
            if (recorder.active === root.wasActive) return
            root.wasActive = recorder.active
            if (recorder.active) {
                root.showSettings = false
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
        spacing: 16

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            LogoMark {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
            }
            Text {
                text: "ELVIDEO"
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 16
                font.weight: Font.DemiBold
                font.letterSpacing: 5
            }
            Text {
                Layout.leftMargin: 6
                text: root.showSettings ? qsTr("설정") : qsTr("녹화할 화면을 고르세요")
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: 13
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
                glyph: root.showSettings ? Theme.iconBack : Theme.iconSettings
                tip: root.showSettings ? qsTr("돌아가기") : qsTr("설정")
                onClicked: root.showSettings = !root.showSettings
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.showSettings ? 1 : 0
            SourcePicker {}
            SettingsPage {}
        }

        Toast {
            id: toast
            Layout.fillWidth: true
        }

        ControlBar {
            Layout.fillWidth: true
        }
    }
}
