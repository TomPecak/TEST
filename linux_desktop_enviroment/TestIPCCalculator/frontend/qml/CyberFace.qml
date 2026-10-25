pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes

import TestCalculator

// The cyberpunk face of the calculator (desktop): neon on pure black, in the style of the Maia
// cyberpunk mockup. Black is #000000 everywhere outside the lines and labels, so on an OLED screen
// those pixels are off and the colors stand out.
//
// The glow is a bloom post-process of the whole face (NeonBloom, own shaders in shaders/): the face is
// rendered into a texture, blurred at 1/2 and 1/4 resolution and the blur is added back like light.
// Black stays black, so on OLED only the neon and its halo are lit. With the software renderer
// (no GPU) there are no shaders: the lines then get a second, wide transparent stroke instead.
//
// Wide window: History / Memory on the right. Narrow window: they slide up over the keypad.
// Everything shown comes from the service (Calc); the shell (Main.qml) owns the window,
// the keyboard and the switching between faces.
Item {
    id: face

    required property var shell
    readonly property string faceName: "cyber"
    readonly property bool wide: width >= 640
    readonly property bool roomy: width >= 420
    readonly property int frameCut: shell.maximized ? 0 : 18
    // Narrow layout: the History / Memory drawer
    property bool drawerOpen: false
    property string panelTab: "history"

    onWideChanged: drawerOpen = false

    // Bloom needs the GPU scene graph; the software renderer has no shader effects
    readonly property bool bloom: GraphicsInfo.api !== GraphicsInfo.Software
    // The neon light: flickers on when the face appears
    property real neonPower: bloom ? 0 : 1

    layer.enabled: bloom
    layer.effect: NeonBloom {
        power: face.neonPower
    }

    SequentialAnimation {
        running: face.bloom
        NumberAnimation { target: face; property: "neonPower"; to: 1; duration: 70 }
        NumberAnimation { target: face; property: "neonPower"; to: 0.15; duration: 50 }
        NumberAnimation { target: face; property: "neonPower"; to: 0.9; duration: 40 }
        PauseAnimation { duration: 60 }
        NumberAnimation { target: face; property: "neonPower"; to: 0.3; duration: 40 }
        NumberAnimation { target: face; property: "neonPower"; to: 1; duration: 260; easing.type: Easing.OutCubic }
    }

    // Called by the shell (Ctrl+H, --panel)
    function togglePanel(tab) {
        if (wide) {
            panelTab = tab
            return
        }
        if (drawerOpen && panelTab === tab) {
            drawerOpen = false
        } else {
            panelTab = tab
            drawerOpen = true
        }
    }

    function openPanel(tab) {
        panelTab = tab
        drawerOpen = true
    }

    // Escape: closes the drawer first
    function closeOverlay() {
        if (!drawerOpen)
            return false
        drawerOpen = false
        return true
    }

    // ========================================================================
    // Palette and type (the colors of the mockup)
    // ========================================================================

    QtObject {
        id: neon

        readonly property color black: "#000000"
        readonly property color panel: "#05080c"
        readonly property color cyan: "#00f0ff"
        readonly property color magenta: "#ff2a6d"
        readonly property color yellow: "#fcee0a"
        readonly property color purple: "#9d4edd"
        readonly property color white: "#e8f6ff"
        // Core of the cyan neon tube (digits): a light cyan, so the halo of the bloom is cyan
        readonly property color tube: "#c8fbff"
        readonly property color dim: "#6e8ca0"
        readonly property color faint: "#33505e"

        readonly property string mono: "IBM Plex Mono"
        readonly property string display: "Ubuntu Sans"

        function alpha(c, a) {
            return Qt.rgba(c.r, c.g, c.b, a)
        }
    }

    // OLED black under everything; the frame below draws the window outline
    Rectangle {
        anchors.fill: parent
        color: neon.black
    }

    Chamfer {
        anchors.fill: parent
        visible: !face.shell.maximized
        cut: face.frameCut
        strokeColor: neon.cyan
        strokeWidth: 1.5
        glow: true
    }

    // Accent bars on the frame, like the panels of the mockup
    Rectangle {
        x: parent.width - width - 40
        y: 0
        width: Math.min(110, parent.width / 4)
        height: 3
        color: neon.yellow
    }
    Rectangle {
        x: 34
        y: parent.height - height
        width: Math.min(150, parent.width / 3)
        height: 3
        color: neon.magenta
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: face.shell.maximized ? 0 : 2
        spacing: 0

        // --- Title bar: move, maximize, face, window buttons ---
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44

            DragHandler {
                target: null
                onActiveChanged: {
                    if (active)
                        face.shell.startSystemMove()
                }
            }
            TapHandler {
                onDoubleTapped: face.shell.maximized ? face.shell.showNormal() : face.shell.showMaximized()
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                spacing: 8

                // Logo: a slanted yellow block, like the logo of the mockup
                Shape {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 14
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        strokeWidth: 0
                        strokeColor: "transparent"
                        fillColor: neon.yellow
                        startX: 7; startY: 0
                        PathLine { x: 26; y: 0 }
                        PathLine { x: 19; y: 14 }
                        PathLine { x: 0; y: 14 }
                        PathLine { x: 7; y: 0 }
                    }
                }

                Text {
                    text: "CALC"
                    color: neon.yellow
                    font.family: neon.display
                    font.variableAxes: ({ "wdth": 75 })
                    font.weight: Font.Bold
                    font.pixelSize: 19
                    font.letterSpacing: 1
                }

                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: face.roomy ? "NETRUNNER // STANDARD" : "// STD"
                    color: neon.cyan
                    font.family: neon.mono
                    font.pixelSize: 10
                    font.letterSpacing: 1
                }

                // Round trip of the last key to the service
                Text {
                    visible: face.roomy && Calc.lastCallMs > 0
                    text: "LINK " + Calc.lastCallMs.toFixed(2) + " MS"
                    color: neon.dim
                    font.family: neon.mono
                    font.pixelSize: 10
                }

                TagButton {
                    text: "SKIN ▸"
                    accent: neon.purple
                    onClicked: face.shell.switchFace(face.shell.followingFace)
                }

                TagButton {
                    visible: !face.wide
                    text: "LOG"
                    accent: neon.cyan
                    checked: face.drawerOpen
                    onClicked: face.togglePanel("history")
                }

                WindowButton {
                    glyph: "minimize"
                    onClicked: face.shell.showMinimized()
                }
                WindowButton {
                    glyph: "maximize"
                    onClicked: face.shell.maximized ? face.shell.showNormal() : face.shell.showMaximized()
                }
                WindowButton {
                    glyph: "close"
                    accent: neon.magenta
                    onClicked: face.shell.close()
                }
            }
        }

        // Shown while the calculator service is not reachable (it reconnects every second)
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Calc.connected ? 0 : 26
            visible: Layout.preferredHeight > 0
            clip: true
            color: neon.alpha(neon.magenta, 0.16)

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: "LINK LOST // CALC SERVICE OFFLINE. RECONNECTING…"
                color: neon.magenta
                font.family: neon.mono
                font.pixelSize: 10
                font.letterSpacing: 1
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // --- Calculator ---
            Item {
                id: calculator

                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: face.wide ? 6 : 12
                    anchors.bottomMargin: 12
                    spacing: 8

                    // --- Display ---
                    Item {
                        id: display

                        Layout.fillWidth: true
                        Layout.preferredHeight: 124
                        Layout.minimumHeight: 104

                        TapHandler {
                            // A click above the drawer closes it
                            enabled: face.drawerOpen
                            onTapped: face.drawerOpen = false
                        }

                        Chamfer {
                            anchors.fill: parent
                            cut: 14
                            fillColor: neon.black
                            strokeColor: Calc.error ? neon.alpha(neon.magenta, 0.8) : neon.alpha(neon.cyan, 0.45)
                        }

                        Text {
                            x: 16
                            y: 10
                            text: Calc.error ? "// ERROR" : "// OUTPUT"
                            color: Calc.error ? neon.magenta : neon.cyan
                            font.family: neon.mono
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            y: 10
                            text: Calc.memory.length > 0 ? "MEM " + String(Calc.memory.length).padStart(2, "0") : "MEM --"
                            color: Calc.memory.length > 0 ? neon.yellow : neon.faint
                            font.family: neon.mono
                            font.pixelSize: 10
                            font.letterSpacing: 1
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            y: 30
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                            text: Calc.expression
                            color: neon.dim
                            font.family: neon.mono
                            font.pixelSize: 14
                        }

                        // The number with a blinking block cursor. Long numbers are scaled down with
                        // `scale`, never with a new font size (each new pixel size costs ~300 ms).
                        Item {
                            id: numberArea

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            anchors.bottomMargin: 10
                            height: 60

                            Row {
                                id: numberRow

                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                spacing: 6
                                transformOrigin: Item.BottomRight
                                scale: implicitWidth > numberArea.width ? numberArea.width / implicitWidth : 1

                                Text {
                                    id: numberText

                                    text: Calc.display
                                    color: Calc.error ? neon.magenta : neon.tube
                                    font.family: neon.display
                                    font.variableAxes: ({ "wdth": 75 })
                                    font.weight: Font.Bold
                                    font.pixelSize: Calc.error ? 30 : 54
                                    font.capitalization: Calc.error ? Font.AllUppercase : Font.MixedCase
                                }

                                Rectangle {
                                    id: cursorBlock

                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: Calc.error ? 6 : 10
                                    width: 10
                                    height: Calc.error ? 20 : 34
                                    color: Calc.error ? neon.magenta : neon.cyan
                                    // Blinks twice a second: a Timer, not an endless animation,
                                    // so the window is redrawn only when it changes
                                    visible: blink.on

                                    Timer {
                                        id: blink

                                        property bool on: true

                                        interval: 530
                                        repeat: true
                                        running: face.visible
                                        onTriggered: on = !on
                                    }
                                }
                            }
                        }
                    }

                    // --- Memory keys ---
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        spacing: 4

                        MemoryButton { text: "MC"; key: "mc"; enabled: Calc.connected && !Calc.error && Calc.memory.length > 0 }
                        MemoryButton { text: "MR"; key: "mr"; enabled: Calc.connected && !Calc.error && Calc.memory.length > 0 }
                        MemoryButton { text: "M+"; key: "m+" }
                        MemoryButton { text: "M−"; key: "m-" }
                        MemoryButton { text: "MS"; key: "ms" }
                        MemoryButton {
                            visible: !face.wide
                            text: "M ▾"
                            enabled: Calc.memory.length > 0
                            onClicked: face.togglePanel("memory")
                        }
                    }

                    // --- Keypad ---
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: 4
                        rowSpacing: 5
                        columnSpacing: 5

                        Key { key: "percent"; label: "%"; kind: "function" }
                        Key { key: "ce"; label: "CE"; kind: "clear"; allowedInError: true }
                        Key { key: "c"; label: "C"; kind: "clear"; allowedInError: true }
                        Key { key: "backspace"; label: "⌫"; kind: "clear"; allowedInError: true }

                        Key { key: "reciprocal"; label: "1/x"; kind: "function" }
                        Key { key: "square"; label: "x²"; kind: "function" }
                        Key { key: "sqrt"; label: "²√x"; kind: "function" }
                        Key { key: "divide"; label: "÷"; kind: "operator" }

                        Key { key: "7"; label: "7"; allowedInError: true }
                        Key { key: "8"; label: "8"; allowedInError: true }
                        Key { key: "9"; label: "9"; allowedInError: true }
                        Key { key: "multiply"; label: "×"; kind: "operator" }

                        Key { key: "4"; label: "4"; allowedInError: true }
                        Key { key: "5"; label: "5"; allowedInError: true }
                        Key { key: "6"; label: "6"; allowedInError: true }
                        Key { key: "subtract"; label: "−"; kind: "operator" }

                        Key { key: "1"; label: "1"; allowedInError: true }
                        Key { key: "2"; label: "2"; allowedInError: true }
                        Key { key: "3"; label: "3"; allowedInError: true }
                        Key { key: "add"; label: "+"; kind: "operator" }

                        Key { key: "negate"; label: "+/−"; kind: "function" }
                        Key { key: "0"; label: "0"; allowedInError: true }
                        Key { key: "decimal"; label: face.shell.decimalPoint; allowedInError: true }
                        Key { key: "equals"; label: "="; kind: "equals"; allowedInError: true }
                    }
                }

                // --- History / Memory drawer of the narrow layout ---
                Item {
                    id: drawer

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    // Covers everything below the display
                    height: parent.height - display.height
                    clip: true
                    visible: !face.wide && (face.drawerOpen || drawerAnimation.running)

                    Item {
                        width: parent.width
                        height: parent.height
                        y: face.drawerOpen ? 0 : parent.height

                        Behavior on y {
                            NumberAnimation {
                                id: drawerAnimation

                                duration: 220
                                easing.type: face.drawerOpen ? Easing.OutCubic : Easing.InCubic
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: neon.black
                        }

                        LogPanel {
                            anchors.fill: parent
                            anchors.margins: 12
                            onChosen: {
                                face.drawerOpen = false
                                face.shell.refocus()
                            }
                        }
                    }
                }
            }

            // --- History / Memory on the right (wide layout) ---
            LogPanel {
                Layout.preferredWidth: 290
                Layout.fillHeight: true
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                visible: face.wide
                onChosen: face.shell.refocus()
            }
        }
    }

    // ========================================================================
    // Building blocks
    // ========================================================================

    // Rectangle with the top left and bottom right corners cut off: the panel shape of the mockup.
    // The path is inset by half of the stroke, so the line stays inside the item.
    component Chamfer: Shape {
        id: chamfer

        property real cut: 10
        property color fillColor: "transparent"
        property color strokeColor: neon.cyan
        property real strokeWidth: 1
        // A wider transparent stroke under the line
        property bool glow: false

        readonly property real inset: strokeWidth / 2
        readonly property real rightEdge: width - inset
        readonly property real bottomEdge: height - inset
        readonly property real c: Math.min(cut, width / 2, height / 2)

        // Smooth edges without multisampling (MSAA of the whole layer cost more than it gave)
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: chamfer.glow && !face.bloom ? 7 : 0
            strokeColor: chamfer.glow && !face.bloom ? Qt.rgba(chamfer.strokeColor.r, chamfer.strokeColor.g, chamfer.strokeColor.b, 0.14) : "transparent"
            fillColor: "transparent"
            joinStyle: ShapePath.MiterJoin
            startX: chamfer.inset + chamfer.c; startY: chamfer.inset
            PathLine { x: chamfer.rightEdge; y: chamfer.inset }
            PathLine { x: chamfer.rightEdge; y: chamfer.bottomEdge - chamfer.c }
            PathLine { x: chamfer.rightEdge - chamfer.c; y: chamfer.bottomEdge }
            PathLine { x: chamfer.inset; y: chamfer.bottomEdge }
            PathLine { x: chamfer.inset; y: chamfer.inset + chamfer.c }
            PathLine { x: chamfer.inset + chamfer.c; y: chamfer.inset }
        }

        ShapePath {
            strokeWidth: chamfer.strokeWidth
            strokeColor: chamfer.strokeColor
            fillColor: chamfer.fillColor
            joinStyle: ShapePath.MiterJoin
            startX: chamfer.inset + chamfer.c; startY: chamfer.inset
            PathLine { x: chamfer.rightEdge; y: chamfer.inset }
            PathLine { x: chamfer.rightEdge; y: chamfer.bottomEdge - chamfer.c }
            PathLine { x: chamfer.rightEdge - chamfer.c; y: chamfer.bottomEdge }
            PathLine { x: chamfer.inset; y: chamfer.bottomEdge }
            PathLine { x: chamfer.inset; y: chamfer.inset + chamfer.c }
            PathLine { x: chamfer.inset + chamfer.c; y: chamfer.inset }
        }
    }

    // Key of the keypad. kind: digit (white), function (cyan), clear (magenta), operator (yellow),
    // equals (yellow block). allowedInError: still usable while an error is shown (digits, clearing).
    component Key: Item {
        id: keyItem

        required property string key
        required property string label
        property string kind: "digit"
        property bool allowedInError: false

        readonly property color accent: kind === "function" ? neon.cyan
                                      : kind === "clear" ? neon.magenta
                                      : kind === "operator" || kind === "equals" ? neon.yellow
                                      : neon.tube
        readonly property bool held: mouse.pressed || face.shell.flashKey === key
        // Filled only while pressed: a filled block under the bloom would wash out its own label
        readonly property bool lit: held

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: 1
        Layout.minimumHeight: 30
        enabled: Calc.connected && (!Calc.error || allowedInError)
        opacity: enabled ? 1 : 0.28

        Chamfer {
            anchors.fill: parent
            cut: 8
            glow: keyItem.lit || mouse.containsMouse || keyItem.kind === "equals"
            fillColor: keyItem.lit ? keyItem.accent
                     : mouse.containsMouse ? neon.alpha(keyItem.accent, 0.12)
                     : neon.black
            strokeColor: keyItem.lit || mouse.containsMouse || keyItem.kind === "equals" ? keyItem.accent
                       : neon.alpha(keyItem.accent, keyItem.kind === "digit" ? 0.35 : 0.7)
            strokeWidth: keyItem.lit || mouse.containsMouse || keyItem.kind === "equals" ? 2 : 1.2
        }

        Text {
            anchors.centerIn: parent
            text: keyItem.label
            color: keyItem.lit ? neon.black : keyItem.accent
            font.family: keyItem.kind === "digit" || keyItem.kind === "equals" ? neon.display : neon.mono
            font.variableAxes: ({ "wdth": 75 })
            font.weight: keyItem.kind === "digit" || keyItem.kind === "equals" || keyItem.kind === "operator" ? Font.Bold : Font.Medium
            font.pixelSize: keyItem.kind === "digit" || keyItem.kind === "equals" || keyItem.kind === "operator" ? 24
                          : keyItem.label === "⌫" ? 22 : 15
        }

        MouseArea {
            id: mouse

            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                face.shell.press(keyItem.key)
                face.shell.refocus()
            }
        }
    }

    component MemoryButton: Item {
        id: memoryButton

        property string text
        property string key
        signal clicked()

        Layout.fillWidth: true
        Layout.fillHeight: true
        enabled: Calc.connected && !Calc.error
        opacity: enabled ? 1 : 0.3

        Rectangle {
            anchors.fill: parent
            color: memoryMouse.pressed ? neon.cyan : memoryMouse.containsMouse ? neon.alpha(neon.cyan, 0.1) : neon.black
            border.width: 1
            border.color: memoryMouse.containsMouse ? neon.cyan : neon.alpha(neon.cyan, 0.25)
        }

        Text {
            anchors.centerIn: parent
            text: memoryButton.text
            color: memoryMouse.pressed ? neon.black : neon.cyan
            font.family: neon.mono
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }

        MouseArea {
            id: memoryMouse

            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                if (memoryButton.key.length > 0) {
                    Calc.press(memoryButton.key)
                    face.shell.noteKey(memoryButton.key)
                }
                memoryButton.clicked()
                face.shell.refocus()
            }
        }
    }

    // Small outlined label button (title bar, panel tabs, panel actions)
    component TagButton: Item {
        id: tag

        property string text
        property color accent: neon.cyan
        property bool checked: false
        signal clicked()

        implicitWidth: tagLabel.implicitWidth + 16
        implicitHeight: 22
        Layout.preferredWidth: implicitWidth
        Layout.preferredHeight: implicitHeight

        Rectangle {
            anchors.fill: parent
            // Checked: lit inside and a full border; a filled block under the bloom would wash out the label
            color: tagMouse.pressed ? neon.alpha(tag.accent, 0.45)
                 : tag.checked ? neon.alpha(tag.accent, 0.22)
                 : tagMouse.containsMouse ? neon.alpha(tag.accent, 0.12) : "transparent"
            border.width: tag.checked ? 1.5 : 1
            border.color: tagMouse.containsMouse || tag.checked ? tag.accent : neon.alpha(tag.accent, 0.45)
        }

        Text {
            id: tagLabel

            anchors.centerIn: parent
            text: tag.text
            color: tag.checked ? neon.white : tag.accent
            font.family: neon.mono
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 1
        }

        MouseArea {
            id: tagMouse

            anchors.fill: parent
            hoverEnabled: true
            onClicked: tag.clicked()
        }
    }

    // Minimize / maximize / close: outlined squares as in the mockup
    component WindowButton: Item {
        id: windowButton

        property string glyph
        property color accent: neon.cyan
        signal clicked()

        Layout.preferredWidth: 26
        Layout.preferredHeight: 22

        Rectangle {
            anchors.fill: parent
            color: windowMouse.pressed ? windowButton.accent : windowMouse.containsMouse ? neon.alpha(windowButton.accent, 0.15) : "transparent"
            border.width: 1
            border.color: windowMouse.containsMouse ? windowButton.accent : neon.alpha(windowButton.accent, 0.4)
        }

        // The glyphs are drawn, so they do not depend on the font
        Rectangle {
            visible: windowButton.glyph === "minimize"
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 3
            width: 8
            height: 1.5
            color: windowMouse.pressed ? neon.black : windowButton.accent
        }
        Rectangle {
            visible: windowButton.glyph === "maximize"
            anchors.centerIn: parent
            width: 8
            height: 8
            color: "transparent"
            border.width: 1.5
            border.color: windowMouse.pressed ? neon.black : windowButton.accent
        }
        Item {
            visible: windowButton.glyph === "close"
            anchors.centerIn: parent
            width: 10
            height: 10

            Rectangle { anchors.centerIn: parent; width: 12; height: 1.5; rotation: 45; color: windowMouse.pressed ? neon.black : windowButton.accent }
            Rectangle { anchors.centerIn: parent; width: 12; height: 1.5; rotation: -45; color: windowMouse.pressed ? neon.black : windowButton.accent }
        }

        MouseArea {
            id: windowMouse

            anchors.fill: parent
            hoverEnabled: true
            onClicked: windowButton.clicked()
        }
    }

    // History / Memory: the tab follows face.panelTab, so the side panel and the drawer agree
    component LogPanel: Item {
        id: log

        readonly property bool history: face.panelTab === "history"
        signal chosen()

        Chamfer {
            anchors.fill: parent
            cut: 14
            fillColor: neon.black
            strokeColor: neon.alpha(neon.purple, 0.55)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                TagButton {
                    text: "HISTORY"
                    accent: neon.purple
                    checked: log.history
                    onClicked: face.panelTab = "history"
                }
                TagButton {
                    text: "MEMORY"
                    accent: neon.purple
                    checked: !log.history
                    onClicked: face.panelTab = "memory"
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: String(log.history ? Calc.history.length : Calc.memory.length).padStart(2, "0") + " REC"
                    color: neon.dim
                    font.family: neon.mono
                    font.pixelSize: 10
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: historyList

                    anchors.fill: parent
                    visible: log.history
                    clip: true
                    spacing: 4
                    boundsBehavior: Flickable.StopAtBounds
                    model: Calc.history

                    delegate: Item {
                        id: historyEntry

                        required property var modelData

                        width: historyList.width
                        height: entryColumn.implicitHeight + 12

                        Rectangle {
                            anchors.fill: parent
                            color: historyMouse.containsMouse ? neon.alpha(neon.cyan, 0.08) : "transparent"
                            border.width: historyMouse.containsMouse ? 1 : 0
                            border.color: neon.alpha(neon.cyan, 0.4)
                        }
                        Rectangle {
                            width: 2
                            height: parent.height
                            color: neon.cyan
                        }

                        Column {
                            id: entryColumn

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideLeft
                                text: historyEntry.modelData.expression
                                color: neon.dim
                                font.family: neon.mono
                                font.pixelSize: 11
                            }
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideLeft
                                text: historyEntry.modelData.result
                                color: neon.cyan
                                font.family: neon.display
                                font.variableAxes: ({ "wdth": 75 })
                                font.weight: Font.Bold
                                font.pixelSize: 22
                            }
                        }

                        MouseArea {
                            id: historyMouse

                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                Calc.recallHistory(historyEntry.modelData.id)
                                log.chosen()
                            }
                        }
                    }
                }

                ListView {
                    id: memoryList

                    anchors.fill: parent
                    visible: !log.history
                    clip: true
                    spacing: 4
                    boundsBehavior: Flickable.StopAtBounds
                    model: Calc.memory

                    delegate: Item {
                        id: memoryEntry

                        required property string modelData
                        required property int index

                        width: memoryList.width
                        height: 58

                        Rectangle {
                            anchors.fill: parent
                            color: memoryMouse.containsMouse ? neon.alpha(neon.yellow, 0.07) : "transparent"
                            border.width: memoryMouse.containsMouse ? 1 : 0
                            border.color: neon.alpha(neon.yellow, 0.4)
                        }
                        Rectangle {
                            width: 2
                            height: parent.height
                            color: neon.yellow
                        }

                        MouseArea {
                            id: memoryMouse

                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                Calc.memoryItem(memoryEntry.index, "recall")
                                log.chosen()
                            }
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            y: 4
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                            text: memoryEntry.modelData
                            color: neon.yellow
                            font.family: neon.display
                            font.variableAxes: ({ "wdth": 75 })
                            font.weight: Font.Bold
                            font.pixelSize: 22
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 5
                            spacing: 4

                            TagButton { text: "MC"; accent: neon.magenta; onClicked: Calc.memoryItem(memoryEntry.index, "clear") }
                            TagButton { text: "M+"; onClicked: Calc.memoryItem(memoryEntry.index, "add") }
                            TagButton { text: "M−"; onClicked: Calc.memoryItem(memoryEntry.index, "subtract") }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: log.history ? Calc.history.length === 0 : Calc.memory.length === 0
                    text: log.history ? "// NO RECORDS YET" : "// MEMORY EMPTY"
                    color: neon.faint
                    font.family: neon.mono
                    font.pixelSize: 11
                    font.letterSpacing: 1
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }
                TagButton {
                    text: log.history ? "PURGE LOG" : "WIPE MEMORY"
                    accent: neon.magenta
                    enabled: Calc.connected && (log.history ? Calc.history.length > 0 : Calc.memory.length > 0)
                    opacity: enabled ? 1 : 0.3
                    onClicked: log.history ? Calc.clearHistory() : Calc.clearMemory()
                }
            }
        }
    }
}
