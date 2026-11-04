import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root

    width: 1160
    height: 720
    minimumWidth: 880
    minimumHeight: 560
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

    // The two halves of the app, in the sidebar: recording ("record", "recordings")
    // and watching ("continue", "folder", "drive" for picking a Drive folder), plus "settings".
    property string section: "record"
    property string folderRoot   // the library folder chosen in the sidebar
    property string folderPath   // the folder shown under it
    readonly property bool recordSide: section === "record" || section === "recordings"
    readonly property bool barShown: recordSide || recorder.active

    function openFolder(path) {
        folderRoot = path
        folderPath = path
        section = "folder"
    }
    function addLibraryFolder(path) {
        if (path.length === 0) return
        const list = appSettings.libraryFolders.slice()
        if (!list.some(f => f.toLowerCase() === path.toLowerCase())) list.push(path)
        appSettings.libraryFolders = list
        openFolder(path)
    }
    // A Drive folder: its path is "drive:<id>", so its name is kept beside it.
    function addDriveFolder(id, name) {
        const path = drive.folderPath(id)
        const names = Object.assign({}, appSettings.libraryNames)
        names[path] = name
        appSettings.libraryNames = names
        addLibraryFolder(path)
    }
    function removeLibraryFolder(path) {
        appSettings.libraryFolders = appSettings.libraryFolders.filter(f => f !== path)
        if (section === "folder" && folderRoot === path) section = "continue"
    }
    function folderName(path) {
        if (drive.isDrive(path)) return appSettings.libraryNames[path] || qsTr("Google Drive 폴더")
        const p = path.replace(/\\/g, "/").replace(/\/+$/, "")
        return p.substring(p.lastIndexOf("/") + 1) || p
    }
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
                root.section = "record"
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

    FolderDialog {
        id: libraryFolderDialog
        title: qsTr("동영상 폴더 추가")
        onAccepted: root.addLibraryFolder(desktop.toLocalFile(selectedFolder))
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar: recording on top, watching below, settings at the bottom.
        Rectangle {
            Layout.preferredWidth: 232
            Layout.fillHeight: true
            color: Theme.sidebar
            Rectangle {  // hairline against the content
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                anchors.topMargin: 18
                spacing: 2

                RowLayout {
                    Layout.leftMargin: 8
                    Layout.bottomMargin: 16
                    spacing: 10
                    LogoMark {
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                    }
                    Text {
                        text: "ELVideo"
                        color: Theme.text
                        font.family: Theme.displayFont
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        font.letterSpacing: Theme.tracking(17)
                    }
                }

                component Heading: Text {
                    Layout.leftMargin: 10
                    Layout.topMargin: 6
                    Layout.bottomMargin: 2
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fine
                    font.weight: Font.DemiBold
                }

                Heading { text: qsTr("녹화") }
                SidebarItem {
                    Layout.fillWidth: true
                    text: qsTr("화면 녹화")
                    glyph: Theme.iconMonitor
                    current: root.section === "record"
                    badge: recorder.active ? Theme.formatTime(recorder.replayMode ? recorder.replayBufferedSeconds * 1000 : recorder.elapsedMs) : ""
                    badgeColor: Theme.record
                    onClicked: root.section = "record"
                }
                SidebarItem {
                    Layout.fillWidth: true
                    text: qsTr("녹화한 파일")
                    glyph: Theme.iconLibrary
                    current: root.section === "recordings"
                    badge: library.videoCount + library.imageCount > 0 ? String(library.videoCount + library.imageCount) : ""
                    onClicked: root.section = "recordings"
                }

                Heading { text: qsTr("재생"); Layout.topMargin: 18 }
                SidebarItem {
                    Layout.fillWidth: true
                    text: qsTr("이어서 보기")
                    glyph: Theme.iconPlay
                    current: root.section === "continue"
                    badge: { const n = progress.revision >= 0 ? progress.inProgress(99).length : 0; return n > 0 ? String(n) : "" }
                    onClicked: root.section = "continue"
                }
                Repeater {
                    model: appSettings.libraryFolders
                    SidebarItem {
                        id: folderItem
                        required property string modelData
                        Layout.fillWidth: true
                        readonly property bool isDrive: drive.isDrive(modelData)
                        text: root.folderName(modelData)
                        glyph: isDrive ? "\uE753" : Theme.iconFolder
                        current: root.section === "folder" && root.folderRoot === modelData
                        onClicked: root.openFolder(modelData)
                        ToolTip.visible: hovered
                        ToolTip.delay: 800
                        ToolTip.text: isDrive ? qsTr("Google Drive · %1").arg(drive.email || root.folderName(modelData)) : desktop.nativePath(modelData)
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: folderMenu.popup()
                        }
                        AppMenu {
                            id: folderMenu
                            AppMenuItem { text: qsTr("열기"); onTriggered: root.openFolder(folderItem.modelData) }
                            AppMenuItem {
                                visible: !folderItem.isDrive
                                height: visible ? implicitHeight : 0
                                text: qsTr("탐색기에서 보기")
                                onTriggered: desktop.openFolder(folderItem.modelData)
                            }
                            AppMenuSeparator {}
                            AppMenuItem {
                                destructive: true
                                text: qsTr("목록에서 빼기")
                                onTriggered: root.removeLibraryFolder(folderItem.modelData)
                            }
                        }
                    }
                }
                SidebarItem {
                    id: addFolderItem
                    Layout.fillWidth: true
                    text: qsTr("폴더 추가…")
                    glyph: "\uE710"
                    addStyle: true
                    current: root.section === "drive"
                    onClicked: addMenu.popup(addFolderItem, 0, addFolderItem.height)
                    AppMenu {
                        id: addMenu
                        AppMenuItem { text: qsTr("이 PC의 폴더…"); onTriggered: libraryFolderDialog.open() }
                        AppMenuItem { text: qsTr("Google Drive 폴더…"); onTriggered: root.section = "drive" }
                    }
                }

                Item { Layout.fillHeight: true }

                SidebarItem {
                    Layout.fillWidth: true
                    visible: desktop.hasPlayer()
                    text: qsTr("파일 열기…")
                    glyph: "\uE8E5"
                    onClicked: desktop.openInPlayer()
                }
                SidebarItem {
                    Layout.fillWidth: true
                    text: qsTr("설정")
                    glyph: Theme.iconSettings
                    current: root.section === "settings"
                    onClicked: root.section = "settings"
                }
            }
        }

        // Content: the section's title, then its page. Record pages run under the
        // floating record bar, which shows them blurred through its frosted glass.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            Layout.topMargin: 20
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                visible: root.section !== "folder" && root.section !== "drive"
                spacing: 12
                Text {
                    text: recorder.active && root.section === "record" ? (recorder.replayMode ? qsTr("리플레이 버퍼") : qsTr("녹화 중"))
                        : ({ record: qsTr("화면 녹화"), recordings: qsTr("녹화한 파일"), continue: qsTr("이어서 보기"),
                             settings: qsTr("설정") })[root.section] ?? ""
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: Theme.largeTitle
                    font.weight: Font.DemiBold
                    font.letterSpacing: Theme.tracking(Theme.largeTitle)
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: !captureSupported && root.recordSide
                    text: qsTr("이 시스템은 화면 캡처를 지원하지 않습니다")
                    color: Theme.danger
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
            }

            Item {
                id: body
                Layout.fillWidth: true
                Layout.fillHeight: true
                readonly property real barInset: root.barShown ? controlBar.height + 16 : 0

                StackLayout {
                    id: pages
                    anchors.fill: parent
                    currentIndex: root.section === "record" ? (recorder.active ? 2 : 0)
                                : root.section === "recordings" ? 3
                                : root.section === "continue" ? 4
                                : root.section === "folder" ? 5
                                : root.section === "drive" ? 6 : 1
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
                        onRecordRequested: root.section = "record"
                    }
                    ContinuePage { bottomInset: body.barInset }
                    FolderPage {
                        rootPath: root.folderRoot
                        rootName: root.folderName(root.folderRoot)
                        path: root.folderPath
                        onPathChanged: root.folderPath = path
                        bottomInset: body.barInset
                    }
                    DriveBrowserPage {
                        bottomInset: body.barInset
                        onPicked: (id, name) => root.addDriveFolder(id, name)
                    }
                }

                Toast {
                    id: toast
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: root.barShown ? controlBar.top : parent.bottom
                    anchors.bottomMargin: root.barShown ? 10 : 0
                }

                // The record bar's frosted glass, next to the pages it blurs.
                Frost {
                    visible: controlBar.visible
                    x: controlBar.x
                    y: controlBar.y
                    width: controlBar.width
                    height: controlBar.height
                    source: pages
                    radius: Theme.barRadius
                }
                ControlBar {
                    id: controlBar
                    visible: root.barShown
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                }
            }
        }
    }
}
