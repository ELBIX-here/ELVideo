import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// Bottom bar of the main window: what will be recorded, record/pause, time and status.
Rectangle {
    id: bar

    implicitHeight: 84
    color: Theme.surface
    border.color: Theme.dark ? Theme.border : "transparent"
    radius: Theme.barRadius
    layer.enabled: !Theme.dark
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Theme.shadow
        shadowBlur: 0.9
        shadowVerticalOffset: 3
        autoPaddingEnabled: true
    }

    readonly property bool active: recorder.active
    readonly property bool replay: recorder.replayMode
    // Per-app audio applies to window recordings only.
    readonly property bool appAudio: appSettings.appAudioOnly && sources.showWindows && !sources.regionMode

    Item {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        readonly property real sideWidth: Math.max(0, (width - controls.width) / 2 - 16)

        // Left: source and output description.
        ColumnLayout {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: parent.sideWidth
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: bar.active ? recorder.recordingTitle
                                 : sources.regionMode ? sources.regionTitle
                                 : (sources.selectedTitle || qsTr("선택된 화면 없음"))
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: bar.replay ? recorder.outputInfo + qsTr(" · 최근 %1초 보관").arg(appSettings.replaySeconds)
                    : bar.active ? recorder.outputInfo
                                 : (appSettings.codec === "hevc" ? "HEVC" : appSettings.codec === "av1" ? "AV1" : "H.264")
                                   + " · " + appSettings.fps + "fps · " + (appSettings.bitrateKbps / 1000) + " Mbps · "
                                   + (appSettings.container === "mp4" || appSettings.codec !== "h264"
                                      ? "MP4" : qsTr("fMP4 (안전 저장)")) + " · "
                                   + (appSettings.captureSystemAudio && appSettings.captureMicrophone
                                      ? (appAudio ? qsTr("앱 소리 + 마이크") : qsTr("시스템 소리 + 마이크"))
                                      : appSettings.captureSystemAudio ? (appAudio ? qsTr("앱 소리") : qsTr("시스템 소리"))
                                      : appSettings.captureMicrophone ? qsTr("마이크") : qsTr("오디오 없음"))
                elide: Text.ElideRight
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            Row {
                visible: bar.active && (recorder.systemLevel >= 0 || recorder.microphoneLevel >= 0)
                spacing: 14
                LevelMeter { glyph: Theme.iconVolume; level: recorder.systemLevel }
                LevelMeter { glyph: Theme.iconMicrophone; level: recorder.microphoneLevel }
            }
        }

        // Center: controls.
        RowLayout {
            id: controls
            anchors.centerIn: parent
            spacing: 12
            // Pause, or "save replay" while the replay buffer runs.
            IconButton {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                round: true
                glyph: bar.replay ? Theme.iconSave : recorder.paused ? Theme.iconPlay : Theme.iconPause
                iconSize: 17
                checkedLook: bar.replay
                tip: bar.replay ? qsTr("리플레이 저장") + "  (" + appSettings.hotkeyReplay + ")"
                                : (recorder.paused ? qsTr("다시 시작") : qsTr("일시정지")) + "  (" + appSettings.hotkeyPause + ")"
                enabled: bar.active
                onClicked: bar.replay ? recorder.saveReplay() : recorder.togglePause()
            }
            RecordButton {
                recording: bar.active
                enabled: !recorder.busy && captureSupported
                onClicked: recorder.toggle()
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: (bar.replay ? qsTr("리플레이 버퍼 끄기") : bar.active ? qsTr("정지") : qsTr("녹화"))
                              + "  (" + appSettings.hotkeyStartStop + ")"
            }
            // Replay buffer on/off: keeps the last N seconds in memory until saved.
            IconButton {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                round: true
                glyph: Theme.iconReplay
                iconSize: 17
                checkedLook: bar.replay
                tip: bar.replay ? qsTr("리플레이 버퍼 끄기")
                                : qsTr("리플레이 버퍼 켜기 — 최근 %1초를 메모리에 두었다가 %2로 저장")
                                      .arg(appSettings.replaySeconds).arg(appSettings.hotkeyReplay)
                enabled: !recorder.busy && captureSupported && (bar.replay || !bar.active)
                onClicked: bar.replay ? recorder.stop() : recorder.startReplay()
            }
            IconButton {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                round: true
                glyph: Theme.iconCamera
                iconSize: 17
                tip: qsTr("화면 캡처") + "  (" + appSettings.hotkeyScreenshot + ")"
                enabled: !screenCapture.busy && !screenCapture.active
                onClicked: screenCapture.start()
            }
        }

        // Right: time and state.
        ColumnLayout {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: parent.sideWidth
            spacing: 2
            Text {
                Layout.alignment: Qt.AlignRight
                text: bar.replay ? Theme.formatTime(recorder.replayBufferedSeconds * 1000) : Theme.formatTime(recorder.elapsedMs)
                color: recorder.state === "recording" ? Theme.text : Theme.textDim
                font.family: Theme.displayFont
                font.pixelSize: 26
                font.weight: Font.Light
                font.features: { "tnum": 1 }  // digits keep their width while counting
            }
            Row {
                Layout.alignment: Qt.AlignRight
                spacing: 6
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 7; height: 7; radius: 3.5
                    color: bar.replay ? Theme.accent
                         : recorder.state === "recording" ? Theme.record
                         : recorder.state === "paused" ? Theme.warning
                         : recorder.state === "error" ? Theme.danger : Theme.textFaint
                    property real blink: 1
                    opacity: recorder.state === "recording" ? blink : 1
                    SequentialAnimation on blink {
                        running: recorder.state === "recording"
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 600 }
                        NumberAnimation { to: 1; duration: 600 }
                    }
                }
                Text {
                    text: bar.replay && recorder.state === "recording" ? qsTr("리플레이 버퍼") : ({
                        idle: qsTr("대기"), starting: qsTr("시작하는 중…"), recording: qsTr("녹화 중"),
                        paused: qsTr("일시정지됨"), stopping: qsTr("저장하는 중…"), error: qsTr("오류")
                    })[recorder.state] + (bar.active && recorder.framesDropped > 0
                                          ? qsTr(" · 드롭 %1").arg(recorder.framesDropped) : "")
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                }
            }
        }
    }
}
