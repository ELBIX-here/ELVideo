pragma Singleton
import QtQuick

// Colors and type shared by every screen; taken from the app icon.
QtObject {
    readonly property color background: "#101217"
    readonly property color surface: "#16181F"
    readonly property color surfaceRaised: "#1E212B"
    readonly property color surfaceHover: "#262A36"
    readonly property color border: "#2C303C"
    readonly property color text: "#ECE8E1"
    readonly property color textDim: "#9A9AA6"
    readonly property color textFaint: "#6A6C78"
    readonly property color accent: "#F2A93B"
    readonly property color record: "#E5484D"
    readonly property color danger: "#E5484D"
    readonly property color success: "#4CC38A"

    readonly property string fontFamily: "Segoe UI"
    readonly property string iconFont: Qt.fontFamilies().indexOf("Segoe Fluent Icons") >= 0
                                        ? "Segoe Fluent Icons" : "Segoe MDL2 Assets"
    readonly property string monoFont: "Cascadia Mono, Consolas"

    readonly property int radius: 10

    // Segoe Fluent Icons / MDL2 code points.
    readonly property string iconSettings: "\uE713"
    readonly property string iconRefresh: "\uE72C"
    readonly property string iconFolder: "\uE8B7"
    readonly property string iconPlay: "\uE768"
    readonly property string iconPause: "\uE769"
    readonly property string iconStop: "\uE71A"
    readonly property string iconBack: "\uE72B"
    readonly property string iconMonitor: "\uE7F4"
    readonly property string iconWindow: "\uE737"
    readonly property string iconClose: "\uE711"
    readonly property string iconWarning: "\uE7BA"
    readonly property string iconCheck: "\uE73E"
    readonly property string iconVolume: "\uE767"
    readonly property string iconMicrophone: "\uE720"

    function formatTime(ms) {
        const s = Math.floor(ms / 1000)
        const pad = n => (n < 10 ? "0" : "") + n
        return pad(Math.floor(s / 3600)) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60)
    }
}
