pragma Singleton
import QtQuick

// Colors, shapes and type shared by every screen, after DESIGN.md (Apple):
// parchment and white (dark: near-black tiles) in layers, hairlines instead of
// shadows, one Action Blue for everything interactive, a red record color.
// Light and dark follow the system unless the appearance setting picks one
// (Main binds `appearance`); the player sets "dark".
QtObject {
    id: theme

    property string appearance: "system"  // "system" | "light" | "dark"
    readonly property bool dark: appearance === "dark"
                                 || (appearance === "system" && Qt.styleHints.colorScheme !== Qt.ColorScheme.Light)

    function pick(light, darkColor) { return theme.dark ? darkColor : light }

    // Layers, back to front. Elevation comes from these steps and from frosted
    // bars, not from shadows.
    readonly property color background: pick("#F5F5F7", "#1D1D1F")       // window (parchment / ink)
    readonly property color surface: pick("#FFFFFF", "#272729")          // cards, grouped lists, bars
    readonly property color surfaceRaised: pick("#FFFFFF", "#2A2A2C")    // popups, menus
    readonly property color surfaceHover: pick("#EDEDF0", "#303032")     // hover on flat things
    readonly property color pressed: pick("#E3E3E6", "#3A3A3C")
    readonly property color fill: pick("#E8E8ED", "#333336")             // tracks, segmented control
    readonly property color thumb: pick("#FFFFFF", "#5C5C60")            // segmented control selection
    readonly property color control: pick("#FAFAFC", "#333336")          // pearl buttons, popup buttons
    readonly property color controlBorder: pick("#E0E0E0", "transparent")
    readonly property color border: pick("#E0E0E0", "#333335")           // card hairlines
    readonly property color separator: pick("#E8E8ED", "#38383A")        // hairlines inside groups
    readonly property color mediaBackground: pick("#E8E8ED", "#000000")  // letterbox behind thumbnails
    readonly property color popupBorder: pick("#D2D2D7", "#3A3A3C")

    // The one shadow in the system, under pictures (thumbnails) resting on a surface.
    readonly property color productShadow: Qt.rgba(0, 0, 0, dark ? 0.45 : 0.22)

    // Frosted bars: this tint over a blur of what scrolls underneath.
    readonly property color frost: pick(Qt.rgba(0.961, 0.961, 0.969, 0.80), Qt.rgba(0.153, 0.153, 0.161, 0.78))

    readonly property color text: pick("#1D1D1F", "#FFFFFF")
    readonly property color textDim: pick("#6E6E73", "#A1A1A6")
    readonly property color textFaint: pick("#AEAEB2", "#636366")

    readonly property color accent: pick("#0066CC", "#2997FF")      // Action Blue / Sky Link Blue
    readonly property color accentFocus: pick("#0071E3", "#2997FF") // focus rings, selection
    readonly property color accentSoft: pick(Qt.rgba(0, 0.4, 0.8, 0.10), Qt.rgba(0.161, 0.592, 1, 0.20))
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

    // Three radii and the pill; nothing in between.
    readonly property int radius: 8        // utility buttons, fields, inline pictures
    readonly property int radiusMd: 11     // popups and menus
    readonly property int cardRadius: 18   // cards, groups, bars
    readonly property int barRadius: 18

    // Type: weights 400 and 600 only; sizes from 17 px up are tracked tight.
    readonly property int largeTitle: 28   // page titles
    readonly property int tagline: 21      // section titles in the header
    readonly property int body: 13
    readonly property int caption: 12
    readonly property int fine: 11
    function tracking(px) { return px >= 17 ? -0.022 * px : 0 }

    // Motion: quick and settled. Everything pressable shrinks to pressScale.
    readonly property int fast: 140
    readonly property int normal: 240
    readonly property real pressScale: 0.95

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
