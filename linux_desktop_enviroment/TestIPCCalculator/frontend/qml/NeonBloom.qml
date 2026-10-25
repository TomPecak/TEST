import QtQuick

// Neon bloom as a layer effect: `layer.enabled: true; layer.effect: NeonBloom {}`.
//
// The item with the layer is rendered into a texture (`source`). From it two glow levels are made:
//   near - blurred at 1/2 resolution: the tight halo around lines and text
//   wide - the near glow blurred again at 1/4 resolution: the soft aura
// Each blur is separable (horizontal pass, then vertical, shaders/blur.frag). shaders/neon.frag adds
// both glows to the sharp scene like light, with extra saturation, a slight chromatic aberration and
// scanlines. The passes run only when the item changes (a key, hover, the blinking cursor), not in
// every frame; at 1/2 and 1/4 resolution they cost little.
//
// Needs the GPU scene graph (ShaderEffect); with the software renderer leave the layer off.
Item {
    id: bloom

    // The texture of the item with the layer (set by the layer)
    property var source

    property real nearStrength: 0.85
    property real wideStrength: 0.45
    property real saturation: 1.5
    property real aberration: 0.5
    property real scanlines: 0.5
    // 0..1: the neon light on / off (for a flicker when it powers on)
    property real power: 1
    // Spread of the blur taps, in pixels of the downscaled textures
    property real nearSpread: 1.4
    property real wideSpread: 2.2

    readonly property real dpr: Screen.devicePixelRatio
    readonly property int halfWidth: Math.max(1, Math.round(width / 2))
    readonly property int halfHeight: Math.max(1, Math.round(height / 2))
    readonly property int quarterWidth: Math.max(1, Math.round(width / 4))
    readonly property int quarterHeight: Math.max(1, Math.round(height / 4))

    // --- Near glow: 1/2 resolution ---

    ShaderEffect {
        id: nearHorizontal

        width: bloom.halfWidth
        height: bloom.halfHeight
        property var source: bloom.source
        property vector2d texelStep: Qt.vector2d(bloom.nearSpread / width, 0)
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }
    ShaderEffectSource {
        id: nearHorizontalTexture

        sourceItem: nearHorizontal
        hideSource: true
        visible: false
    }
    ShaderEffect {
        id: nearVertical

        width: bloom.halfWidth
        height: bloom.halfHeight
        property var source: nearHorizontalTexture
        property vector2d texelStep: Qt.vector2d(0, bloom.nearSpread / height)
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }
    ShaderEffectSource {
        id: nearTexture

        sourceItem: nearVertical
        hideSource: true
        visible: false
    }

    // --- Wide glow: the near glow at 1/4 resolution, blurred more ---

    ShaderEffect {
        id: wideHorizontal

        width: bloom.quarterWidth
        height: bloom.quarterHeight
        property var source: nearTexture
        property vector2d texelStep: Qt.vector2d(bloom.wideSpread / width, 0)
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }
    ShaderEffectSource {
        id: wideHorizontalTexture

        sourceItem: wideHorizontal
        hideSource: true
        visible: false
    }
    ShaderEffect {
        id: wideVertical

        width: bloom.quarterWidth
        height: bloom.quarterHeight
        property var source: wideHorizontalTexture
        property vector2d texelStep: Qt.vector2d(0, bloom.wideSpread / height)
        fragmentShader: "qrc:/shaders/blur.frag.qsb"
    }
    ShaderEffectSource {
        id: wideTexture

        sourceItem: wideVertical
        hideSource: true
        visible: false
    }

    // --- The sharp scene plus both glows ---

    ShaderEffect {
        anchors.fill: parent

        property var source: bloom.source
        property var glowNear: nearTexture
        property var glowWide: wideTexture
        property real nearStrength: bloom.nearStrength
        property real wideStrength: bloom.wideStrength
        property real saturation: bloom.saturation
        property real aberration: bloom.aberration * bloom.dpr
        property real scanlines: bloom.scanlines
        property real power: bloom.power
        property vector2d resolution: Qt.vector2d(bloom.width * bloom.dpr, bloom.height * bloom.dpr)
        fragmentShader: "qrc:/shaders/neon.frag.qsb"
    }
}
