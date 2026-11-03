import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// All settings; every change is saved right away.
Item {
    id: page

    component Section: ColumnLayout {
        id: section
        property string title
        default property alias rows: body.data
        Layout.fillWidth: true
        spacing: 10
        Text {
            text: section.title
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.letterSpacing: 1.5
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: body.implicitHeight + 32
            radius: Theme.radius + 2
            color: Theme.surface
            border.color: Theme.border
            ColumnLayout {
                id: body
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
            }
        }
    }

    readonly property string outputDir: appSettings.outputDir || appSettings.defaultOutputDir

    // Device lists, re-read whenever the page is shown (devices come and go).
    property var outputDevices: []
    property var inputDevices: []
    function reloadDevices() {
        outputDevices = recorder.audioDevices(false)
        inputDevices = recorder.audioDevices(true)
    }
    onVisibleChanged: if (visible) reloadDevices()
    Component.onCompleted: reloadDevices()

    // "Windows default (name)" first, then each device; value is the device id ("" = default).
    component DeviceBox: ComboBox {
        id: box
        property var devices: []
        property string deviceId
        signal picked(string id)

        readonly property var entries: {
            const def = devices.find(d => d.isDefault)
            const list = [{ id: "", name: def ? qsTr("Windows 기본 장치 (%1)").arg(def.name) : qsTr("Windows 기본 장치") }]
            for (const d of devices) list.push({ id: d.id, name: d.name })
            // A saved device that is unplugged right now stays selectable.
            if (deviceId.length > 0 && !devices.some(d => d.id === deviceId))
                list.push({ id: deviceId, name: qsTr("연결되지 않은 장치") })
            return list
        }
        width: 340
        model: entries
        textRole: "name"
        currentIndex: Math.max(0, entries.findIndex(e => e.id === deviceId))
        onActivated: index => box.picked(entries[index].id)
    }

    component VolumeSlider: Row {
        id: volume
        property int value
        signal moved(int value)
        spacing: 8
        Slider {
            id: slider
            width: 180
            from: 0
            to: 200
            stepSize: 5
            value: volume.value
            onMoved: volume.moved(Math.round(value))
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 44
            text: Math.round(slider.value) + "%"
            color: Theme.textDim
            font.family: Theme.fontFamily
            font.pixelSize: 12
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("녹화 저장 폴더")
        currentFolder: desktop.toUrl(page.outputDir)
        onAccepted: appSettings.outputDir = desktop.toLocalFile(selectedFolder)
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: scroll.availableWidth - 12
            spacing: 24

            Section {
                title: qsTr("저장")
                SettingRow {
                    label: qsTr("저장 폴더")
                    description: desktop.nativePath(page.outputDir)
                    Row {
                        spacing: 8
                        Button {
                            text: qsTr("변경…")
                            onClicked: folderDialog.open()
                        }
                        Button {
                            text: qsTr("열기")
                            onClicked: desktop.openFolder(page.outputDir)
                        }
                    }
                }
                SettingRow {
                    label: qsTr("파일 형식")
                    description: appSettings.container === "mp4"
                                 ? qsTr("일반 MP4. 녹화 중 앱이 비정상 종료되면 파일을 재생할 수 없습니다.")
                                 : qsTr("조각난 MP4(fMP4). 비정상 종료되어도 그때까지 녹화된 부분이 남습니다.")
                    ComboBox {
                        width: 200
                        model: [qsTr("fMP4 (권장)"), "MP4"]
                        currentIndex: appSettings.container === "mp4" ? 1 : 0
                        onActivated: index => appSettings.container = index === 1 ? "mp4" : "fmp4"
                    }
                }
            }

            Section {
                title: qsTr("비디오")
                SettingRow {
                    label: qsTr("프레임 속도")
                    ComboBox {
                        width: 200
                        readonly property var values: [24, 30, 60, 120]
                        model: values.map(v => v + " fps")
                        currentIndex: Math.max(0, values.indexOf(appSettings.fps))
                        onActivated: index => appSettings.fps = values[index]
                    }
                }
                SettingRow {
                    label: qsTr("비트레이트")
                    description: qsTr("1080p60은 12 Mbps, 1440p60은 20 Mbps, 4K60은 40 Mbps 정도를 권장합니다.")
                    SpinBox {
                        width: 200
                        from: 1
                        to: 150
                        editable: true
                        value: Math.round(appSettings.bitrateKbps / 1000)
                        textFromValue: (v, locale) => v + " Mbps"
                        valueFromText: (text, locale) => parseInt(text)
                        onValueModified: appSettings.bitrateKbps = value * 1000
                    }
                }
                SettingRow {
                    label: qsTr("하드웨어 인코더 사용")
                    description: qsTr("GPU 인코더(Quick Sync, NVENC, AMF)를 씁니다. 끄면 CPU로 인코딩합니다.")
                    ToggleSwitch {
                        checked: appSettings.preferHardware
                        onToggled: appSettings.preferHardware = checked
                    }
                }
                SettingRow {
                    label: qsTr("마우스 커서 녹화")
                    ToggleSwitch {
                        checked: appSettings.captureCursor
                        onToggled: appSettings.captureCursor = checked
                    }
                }
            }

            Section {
                title: qsTr("오디오")
                SettingRow {
                    label: qsTr("시스템 소리 녹음")
                    description: qsTr("스피커나 이어폰으로 나오는 모든 소리를 녹음합니다.")
                    ToggleSwitch {
                        checked: appSettings.captureSystemAudio
                        onToggled: appSettings.captureSystemAudio = checked
                    }
                }
                SettingRow {
                    visible: appSettings.captureSystemAudio
                    label: qsTr("출력 장치")
                    DeviceBox {
                        devices: page.outputDevices
                        deviceId: appSettings.systemAudioDevice
                        onPicked: id => appSettings.systemAudioDevice = id
                    }
                }
                SettingRow {
                    visible: appSettings.captureSystemAudio
                    label: qsTr("시스템 소리 볼륨")
                    VolumeSlider {
                        value: appSettings.systemAudioVolume
                        onMoved: v => appSettings.systemAudioVolume = v
                    }
                }
                SettingRow {
                    label: qsTr("마이크 녹음")
                    description: qsTr("시스템 소리와 함께 하나의 오디오 트랙으로 섞습니다.")
                    ToggleSwitch {
                        checked: appSettings.captureMicrophone
                        onToggled: appSettings.captureMicrophone = checked
                    }
                }
                SettingRow {
                    visible: appSettings.captureMicrophone
                    label: qsTr("마이크")
                    DeviceBox {
                        devices: page.inputDevices
                        deviceId: appSettings.microphoneDevice
                        onPicked: id => appSettings.microphoneDevice = id
                    }
                }
                SettingRow {
                    visible: appSettings.captureMicrophone
                    label: qsTr("마이크 볼륨")
                    VolumeSlider {
                        value: appSettings.microphoneVolume
                        onMoved: v => appSettings.microphoneVolume = v
                    }
                }
                SettingRow {
                    visible: appSettings.captureSystemAudio || appSettings.captureMicrophone
                    label: qsTr("오디오 비트레이트")
                    description: qsTr("AAC 48 kHz 스테레오")
                    ComboBox {
                        width: 200
                        readonly property var values: [96, 128, 160, 192]
                        model: values.map(v => v + " kbps")
                        currentIndex: Math.max(0, values.indexOf(appSettings.audioBitrateKbps))
                        onActivated: index => appSettings.audioBitrateKbps = values[index]
                    }
                }
            }

            Section {
                title: qsTr("단축키")
                SettingRow {
                    label: qsTr("녹화 시작 / 정지")
                    description: qsTr("다른 앱을 쓰는 중에도 동작합니다.")
                    error: hotkeys.startStopError
                    HotkeyField {
                        sequence: appSettings.hotkeyStartStop
                        onEdited: s => appSettings.hotkeyStartStop = s
                    }
                }
                SettingRow {
                    label: qsTr("일시정지 / 다시 시작")
                    error: hotkeys.pauseError
                    HotkeyField {
                        sequence: appSettings.hotkeyPause
                        onEdited: s => appSettings.hotkeyPause = s
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("필드를 누른 뒤 키 조합을 입력하세요. Backspace는 해제, Esc는 취소입니다.")
                        color: Theme.textFaint
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Button {
                        text: qsTr("기본값")
                        onClicked: appSettings.resetHotkeys()
                    }
                }
            }

            Section {
                title: qsTr("동작")
                SettingRow {
                    label: qsTr("녹화 중 메인 창 숨기기")
                    description: qsTr("녹화가 끝나면 다시 나타납니다. ELVideo 창은 숨기지 않아도 녹화 영상에 찍히지 않습니다.")
                    ToggleSwitch {
                        checked: appSettings.hideWindowWhileRecording
                        onToggled: appSettings.hideWindowWhileRecording = checked
                    }
                }
                SettingRow {
                    label: qsTr("플로팅 컨트롤 바 표시")
                    description: qsTr("녹화 중 화면 위에 작은 바를 띄웁니다. 녹화 영상에는 찍히지 않습니다.")
                    ToggleSwitch {
                        checked: appSettings.showFloatingBar
                        onToggled: appSettings.showFloatingBar = checked
                    }
                }
                SettingRow {
                    label: qsTr("닫기 버튼으로 트레이로 최소화")
                    description: qsTr("끄면 창을 닫을 때 앱이 종료됩니다.")
                    ToggleSwitch {
                        checked: appSettings.closeToTray
                        onToggled: appSettings.closeToTray = checked
                    }
                }
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 8
                text: "ELVideo " + engineVersion + "  ·  " + desktop.nativePath(appSettings.filePath)
                color: Theme.textFaint
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
        }
    }
}
