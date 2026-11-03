import QtQuick
import QtQuick.Effects

// The one shadow in the system (DESIGN.md): a soft drop under a picture resting
// on a surface — thumbnails and previews. Fill the picture's area with it and
// put the picture on top; never use it under chrome (bars, cards, buttons).
Rectangle {
    radius: Theme.radius
    color: Theme.mediaBackground
    layer.enabled: true
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: Theme.productShadow
        shadowHorizontalOffset: 3
        shadowVerticalOffset: 5
        blurMax: 32
        shadowBlur: 0.9  // about 30 px
        autoPaddingEnabled: true
    }
}
