import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Picks a Google Drive folder for the library: My Drive, files shared with me
// and shared drives at the top, then folder by folder, with how many videos the
// folder holds so the lecture folder is easy to tell. The Android app's
// DriveBrowser, laid out like a folder page.
Item {
    id: page

    property real bottomInset: 0
    signal picked(string id, string name)

    // Where we are: [{ id, name }] from the top ("Google Drive") down.
    property var trail: []
    readonly property var here: trail.length > 0 ? trail[trail.length - 1] : null
    readonly property bool canPick: here !== null && here.id !== "shared"

    function go(list) {
        trail = list
        drive.browse(here ? here.id : "")
    }
    function enter(item) { go(trail.concat([{ id: item.id, name: item.name }])) }

    onVisibleChanged: if (visible) go([])
    Connections {
        target: drive
        function onSignedIn() { if (page.visible) page.go(page.trail) }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.implicitHeight + page.bottomInset + 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            width: flick.width - 12
            spacing: 22

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                // Breadcrumb: Google Drive › 내 드라이브 › …
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: [{ id: "", name: qsTr("Google Drive") }].concat(page.trail)
                        Row {
                            id: crumb
                            required property var modelData
                            required property int index
                            spacing: 4
                            Text {
                                visible: crumb.index > 0
                                text: "›"
                                color: Theme.textFaint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.caption
                            }
                            Text {
                                readonly property bool last: crumb.index === page.trail.length
                                text: crumb.modelData.name
                                color: last ? Theme.textDim : Theme.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.caption
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: !parent.last
                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: page.go(page.trail.slice(0, crumb.index))
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: page.here ? page.here.name : qsTr("Google Drive 폴더 추가")
                            elide: Text.ElideRight
                            color: Theme.text
                            font.family: Theme.displayFont
                            font.pixelSize: Theme.largeTitle
                            font.weight: Font.DemiBold
                            font.letterSpacing: Theme.tracking(Theme.largeTitle)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: !drive.connected ? qsTr("Google 계정을 연결하면 Drive의 강의 폴더를 바로 재생할 수 있습니다.")
                                : page.here === null ? (drive.email.length > 0 ? qsTr("%1 · 강의가 들어 있는 폴더로 들어가서 추가하세요.").arg(drive.email)
                                                                               : qsTr("강의가 들어 있는 폴더로 들어가서 추가하세요."))
                                : !page.canPick ? qsTr("다른 사람이 공유한 폴더")
                                : drive.browseLoading || drive.browseVideoCount < 0 ? qsTr("불러오는 중…")
                                : qsTr("이 폴더의 동영상 %1개 · 하위 폴더까지 함께 추가됩니다").arg(drive.browseVideoCount)
                            elide: Text.ElideRight
                            color: Theme.textDim
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.body
                        }
                    }
                    AppButton {
                        Layout.alignment: Qt.AlignBottom
                        visible: page.canPick
                        primary: true
                        text: qsTr("이 폴더 추가")
                        onClicked: page.picked(page.here.id, page.here.name)
                    }
                }
            }

            // Not signed in, or Drive could not be read.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 40
                visible: !drive.connected || (!drive.browseLoading && drive.error.length > 0 && drive.browseItems.length === 0)
                spacing: 8
                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    glyph: ""  // cloud
                    size: 40
                    color: Theme.textFaint
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: !drive.connected ? qsTr("Google Drive에 연결되어 있지 않아요") : qsTr("Drive를 열 수 없어요")
                    color: Theme.text
                    font.family: Theme.displayFont
                    font.pixelSize: Theme.tagline
                    font.weight: Font.DemiBold
                    font.letterSpacing: Theme.tracking(Theme.tagline)
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: 460
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: drive.busy ? qsTr("브라우저에서 Google 로그인을 마치면 여기로 돌아옵니다.")
                        : drive.error.length > 0 ? drive.error
                        : !drive.configured ? qsTr("이 빌드에는 Google 로그인 설정이 없습니다.")
                        : qsTr("브라우저가 열리면 Google 계정을 고르고 Drive 읽기를 허용하세요. ELVideo는 Drive의 파일을 바꾸지 않습니다.")
                    color: Theme.textDim
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.body
                }
                AppButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 8
                    visible: drive.configured
                    primary: true
                    enabled: !drive.busy
                    text: drive.busy ? qsTr("로그인 기다리는 중…") : drive.connected ? qsTr("다시 시도") : qsTr("Google 계정 연결")
                    onClicked: drive.connected ? page.go(page.trail) : drive.signIn()
                }
            }

            // The folders here.
            Rectangle {
                Layout.fillWidth: true
                visible: drive.connected && drive.browseItems.length > 0
                implicitHeight: list.implicitHeight
                radius: Theme.cardRadius
                color: Theme.surface
                border.color: Theme.border
                Column {
                    id: list
                    width: parent.width
                    Repeater {
                        model: drive.browseItems
                        AbstractButton {
                            id: folder
                            required property var modelData
                            required property int index
                            width: list.width
                            implicitHeight: 52
                            hoverEnabled: true
                            Accessible.name: modelData.name
                            onClicked: page.enter(modelData)
                            background: Rectangle {
                                anchors.fill: parent
                                anchors.margins: 1
                                radius: Theme.cardRadius - 1
                                color: folder.down ? Theme.pressed : folder.hovered ? Theme.surfaceHover : "transparent"
                                Rectangle {  // hairline between rows
                                    visible: folder.index > 0
                                    x: 52
                                    width: parent.width - 52
                                    height: 1
                                    color: Theme.separator
                                }
                            }
                            contentItem: RowLayout {
                                spacing: 14
                                Icon {
                                    Layout.leftMargin: 18
                                    glyph: folder.modelData.kind === "drive" ? "" : Theme.iconFolder
                                    size: 18
                                    color: Theme.accent
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: folder.modelData.name
                                    elide: Text.ElideRight
                                    color: Theme.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.body
                                }
                                Icon {
                                    Layout.rightMargin: 16
                                    glyph: ""  // chevron right
                                    size: 11
                                    color: Theme.textFaint
                                }
                            }
                        }
                    }
                }
            }

            Text {
                visible: drive.connected && drive.browseLoading
                text: qsTr("불러오는 중…")
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.body
            }
            Text {
                visible: drive.connected && !drive.browseLoading && drive.browseItems.length === 0 && page.here !== null && drive.error.length === 0
                text: qsTr("하위 폴더가 없습니다")
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.body
            }
        }
    }
}
