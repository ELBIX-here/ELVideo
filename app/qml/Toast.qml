import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// Notification banner above the record bar: "saved" with file actions, or an
// error. Slides up and fades in; stays while the mouse is over it.
Rectangle {
    id: toast

    property string message
    property string filePath
    property bool isError: false
    property bool isWarning: false
    property string folderPath  // shows an "open folder" button
    property string openLabel: qsTr("재생")
    property bool video: true  // open in ELVideo's player rather than the default app

    function showSaved(text, path, openText) {
        message = text
        filePath = path || ""
        openLabel = openText || qsTr("재생")
        video = !openText
        folderPath = ""
        isError = false
        isWarning = false
        open()
    }
    function showError(text) {
        message = text
        filePath = ""
        folderPath = ""
        isError = true
        isWarning = false
        open()
    }
    function showWarning(text, folder) {
        message = text
        filePath = ""
        folderPath = folder || ""
        isError = false
        isWarning = true
        open()
    }
    function open() {
        shown = true
        hideTimer.restart()
    }

    property bool shown: false

    implicitHeight: row.implicitHeight + 20
    radius: Theme.cardRadius
    color: Theme.surfaceRaised
    border.color: Theme.dark ? Theme.border : "transparent"
    opacity: shown ? 1 : 0
    visible: opacity > 0
    transform: Translate { y: toast.shown ? 0 : 10; Behavior on y { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } } }
    Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    layer.enabled: !Theme.dark
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Theme.shadow
        shadowBlur: 0.8
        shadowVerticalOffset: 2
        autoPaddingEnabled: true
    }

    Timer {
        id: hideTimer
        interval: toast.isError || toast.isWarning ? 12000 : 8000
        onTriggered: toast.shown = false
    }
    HoverHandler {
        onHoveredChanged: hovered ? hideTimer.stop() : hideTimer.restart()
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 6
        spacing: 10

        // Symbol in a tinted circle.
        Rectangle {
            Layout.preferredWidth: 26
            Layout.preferredHeight: 26
            radius: 13
            color: toast.isError ? Theme.danger : toast.isWarning ? Theme.warning : Theme.success
            Icon {
                anchors.centerIn: parent
                glyph: toast.isError || toast.isWarning ? "" : Theme.iconCheck
                size: 12
                color: "#FFFFFF"
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: toast.message
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                visible: toast.filePath.length > 0
                text: desktop.fileName(toast.filePath)
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }
        }
        AppButton {
            visible: toast.folderPath.length > 0
            text: qsTr("폴더 열기")
            onClicked: desktop.openFolder(toast.folderPath)
        }
        AppButton {
            visible: toast.filePath.length > 0
            primary: true
            text: toast.openLabel
            onClicked: toast.video ? desktop.openInPlayer(toast.filePath) : desktop.openFile(toast.filePath)
        }
        AppButton {
            visible: toast.filePath.length > 0
            text: qsTr("폴더에서 보기")
            onClicked: desktop.revealFile(toast.filePath)
        }
        IconButton {
            glyph: Theme.iconClose
            iconSize: 11
            round: true
            implicitWidth: 28
            implicitHeight: 28
            tip: qsTr("닫기")
            onClicked: toast.shown = false
        }
    }
}
