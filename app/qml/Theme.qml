pragma Singleton
import QtQuick

// Colors and type shared by every screen. Light and dark follow the system
// unless the appearance setting picks one (Main binds `appearance`); the
// player sets "dark". The palette follows Apple's system colors: neutral
// grays in layers, hairline separators, one blue accent and a red record color.
QtObject {
    id: theme

    property string appearance: "system"  // "system" | "light" | "dark"
    readonly property bool dark: appearance === "dark"
                                 || (appearance === "system" && Qt.styleHints.colorScheme !== Qt.ColorScheme.Light)

    function pick(light, darkColor) { return theme.dark ? darkColor : light }

    // Layers, back to front.
    readonly property color background: pick("#F5F5F7", "#161617")       // window
    readonly property color surface: pick("#FFFFFF", "#232325")          // cards, grouped lists, bars
    readonly property color surfaceRaised: pick("#FFFFFF", "#2C2C2E")    // selected card, popups
    readonly property color surfaceHover: pick("#EDEDF0", "#2F2F32")     // hover on flat things
    readonly property color pressed: pick("#E1E1E6", "#3A3A3D")
    readonly property color fill: pick("#E6E6EB", "#2C2C2F")             // tracks, segmented control
    readonly property color thumb: pick("#FFFFFF", "#5C5C61")            // segmented control selection
    readonly property color control: pick("#FFFFFF", "#3A3A3C")          // push buttons, popup buttons
    readonly property color controlBorder: pick("#D2D2D7", "transparent")
    readonly property color border: pick("#E3E3E8", "#2E2E31")           // card outlines
    readonly property color separator: pick("#E5E5EA", "#38383A")        // hairlines inside groups
    readonly property color mediaBackground: pick("#E8E8ED", "#0E0E0F")  // letterbox behind thumbnails
    readonly property color shadow: pick(Qt.rgba(0, 0, 0, 0.10), Qt.rgba(0, 0, 0, 0.45))

    readonly property color text: pick("#1D1D1F", "#F5F5F7")
    readonly property color textDim: pick("#6E6E73", "#A1A1A6")
    readonly property color textFaint: pick("#AEAEB2", "#636366")

    readonly property color accent: pick("#007AFF", "#0A84FF")
    readonly property color accentSoft: pick(Qt.rgba(0, 0.478, 1, 0.12), Qt.rgba(0.039, 0.518, 1, 0.22))
    readonly property color accentText: "#FFFFFF"
    readonly property color record: pick("#FF3B30", "#FF453A")
    readonly property color danger: record
    readonly property color warning: pick("#FF9500", "#FF9F0A")
    readonly property color success: pick("#34C759", "#30D158")

    // Floating HUDs (recording bar, countdown, overlays) are dark in both modes.
    readonly property color hud: Qt.rgba(0.11, 0.11, 0.12, 0.88)
    readonly property color hudText: "#F5F5F7"
    readonly property color hudTextDim: "#A1A1A6"
    readonly property color hudHover: Qt.rgba(1, 1, 1, 0.10)

    readonly property string fontFamily: Qt.fontFamilies().indexOf("Segoe UI Variable Text") >= 0
                                         ? "Segoe UI Variable Text" : "Segoe UI"
    readonly property string displayFont: Qt.fontFamilies().indexOf("Segoe UI Variable Display") >= 0
                                          ? "Segoe UI Variable Display" : "Segoe UI"
    readonly property string iconFont: Qt.fontFamilies().indexOf("Segoe Fluent Icons") >= 0
                                        ? "Segoe Fluent Icons" : "Segoe MDL2 Assets"
    readonly property string monoFont: "Cascadia Mono, Consolas"

    readonly property int radius: 10       // controls
    readonly property int cardRadius: 12   // cards and groups
    readonly property int barRadius: 16    // floating bars

    // Motion: quick and settled, with a little overshoot only where things pop in.
    readonly property int fast: 140
    readonly property int normal: 240

    // Segoe Fluent Icons / MDL2 code points.
    readonly property string iconSettings: ""
    readonly property string iconRefresh: ""
    readonly property string iconFolder: ""
    readonly property string iconPlay: ""
    readonly property string iconPause: ""
    readonly property string iconStop: ""
    readonly property string iconBack: ""
    readonly property string iconMonitor: ""
    readonly property string iconWindow: ""
    readonly property string iconClose: ""
    readonly property string iconWarning: ""
    readonly property string iconCheck: ""
    readonly property string iconVolume: ""
    readonly property string iconMicrophone: ""
    readonly property string iconSave: ""
    readonly property string iconReplay: ""
    readonly property string iconCamera: ""
    readonly property string iconLibrary: ""
    readonly property string iconSearch: ""
    readonly property string iconChevronDown: ""
    readonly property string iconChevronUp: ""
    readonly property string iconRegion: ""

    function formatTime(ms) {
        const s = Math.floor(ms / 1000)
        const pad = n => (n < 10 ? "0" : "") + n
        return pad(Math.floor(s / 3600)) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60)
    }
}
