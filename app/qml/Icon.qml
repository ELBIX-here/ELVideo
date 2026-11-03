import QtQuick

// One glyph from the system icon font.
Text {
    property string glyph
    property real size: 16

    text: glyph
    font.family: Theme.iconFont
    font.pixelSize: size
    color: Theme.text
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    renderType: Text.CurveRendering  // no subpixel AA: ClearType fringes on translucent windows
}
