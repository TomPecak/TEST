pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

import TestCalculator

// The iPadOS 18 face of the calculator.
//
// Round keys, orange operators (the pending one stays highlighted), "AC" that turns into "⌫"
// while a number is typed, a large light number. History is a sidebar on the left; the
// calculator key at the bottom left shows more functions (√, x², 1/x, memory). Instead of window
// buttons there is the "•••" multitasking menu at the top, like windows in Stage Manager.
//
// It is only a look: the session, history and memory are the same as in the Lumen face.
Item {
    id: face

    required property var shell
    readonly property string faceName: "ipad"
    readonly property bool wide: width >= 600
    readonly property int cornerRadius: shell.maximized ? 0 : 18
    property bool sidebarOpen: false
    // The extra keys: what the user wants (extraOpen) and whether they take their place in the
    // layout (extrasLaid: set before they appear, cleared after they disappear)
    property bool extraOpen: false
    property bool extrasLaid: false
    // Resizing the window must not change any font size: every new pixel size makes Qt create new
    // font engines (with font fallback lookups), ~300 ms each time (measured with --resize-bench).
    // Texts have fixed sizes and are scaled with a transform instead (distance field text stays sharp).
    readonly property real numberSize: Math.max(48, Math.min(96, height * 0.14))
    // Size of a key cell of the keypad; key labels are designed for 96 and scaled to it
    readonly property real keyUnit: Math.max(24, Math.min((keypadMover.width - 30) / 4, (keypadArea.height - 40) / 5))
    readonly property real keyScale: keyUnit / 96
    // Size of the number while the extra keys of the narrow layout cover the bottom of the display
    readonly property real compactScale: Math.max(0.4, Math.min(1, (display.height - 98) / (numberSize * 1.25)))

    property bool instantExtras: false

    onExtraOpenChanged: {
        if (instantExtras) {
            showExtras.stop()
            hideExtras.stop()
            applyExtras()
        } else if (extraOpen) {
            hideExtras.stop()
            showExtras.restart()
        } else {
            showExtras.stop()
            hideExtras.restart()
        }
    }
    // Switching between the narrow and the wide layout: the final state at once
    onWideChanged: {
        showExtras.stop()
        hideExtras.stop()
        applyExtras()
    }

    // Width changes (resizing the window) keep the final state, unless an animation runs
    function syncExtras() {
        if (!showExtras.running && !hideExtras.running)
            applyExtras()
    }

    // Final state without animation
    function applyExtras() {
        extrasLaid = extraOpen
        wideExtras.opacity = extraOpen ? 1 : 0
        narrowExtras.opacity = extraOpen ? 1 : 0
        narrowExtras.y = narrowExtras.restY
        keypadMover.x = wide && extraOpen ? keypadArea.extraWidth + 10 : 0
        numberLayer.y = !wide && extraOpen ? -(narrowExtras.height + 10) : 0
        numberLayer.scale = !wide && extraOpen ? compactScale : 1
        expressionText.opacity = !wide && extraOpen ? 0 : 1
    }

    // Show: the layout changes once (the keypad gets narrower in the wide layout), then everything
    // moves with animators: keys fade and slide in, the keypad slides right, the number shrinks
    SequentialAnimation {
        id: showExtras

        ScriptAction {
            script: {
                face.extrasLaid = true
                narrowExtras.y = narrowExtras.restY + 18
                wideExtras.x = -16
            }
        }
        ParallelAnimation {
            OpacityAnimator { target: wideExtras; to: 1; duration: 240; easing.type: Easing.OutCubic }
            XAnimator { target: wideExtras; to: 0; duration: 280; easing.type: Easing.OutCubic }
            XAnimator { target: keypadMover; to: face.wide ? keypadArea.extraWidth + 10 : 0; duration: 280; easing.type: Easing.OutCubic }
            OpacityAnimator { target: narrowExtras; to: 1; duration: 220; easing.type: Easing.OutCubic }
            YAnimator { target: narrowExtras; to: narrowExtras.restY; duration: 280; easing.type: Easing.OutCubic }
            YAnimator { target: numberLayer; to: face.wide ? 0 : -(narrowExtras.height + 10); duration: 280; easing.type: Easing.OutCubic }
            ScaleAnimator { target: numberLayer; to: face.wide ? 1 : face.compactScale; duration: 280; easing.type: Easing.OutCubic }
            OpacityAnimator { target: expressionText; to: face.wide ? 1 : 0; duration: 160 }
        }
        // Exact final values (also when the window changed its size meanwhile)
        ScriptAction { script: face.applyExtras() }
    }

    // Hide: the reverse, and the layout changes back at the end
    SequentialAnimation {
        id: hideExtras

        ParallelAnimation {
            OpacityAnimator { target: wideExtras; to: 0; duration: 180; easing.type: Easing.InCubic }
            XAnimator { target: wideExtras; to: -16; duration: 220; easing.type: Easing.InCubic }
            XAnimator { target: keypadMover; to: 0; duration: 240; easing.type: Easing.OutCubic }
            OpacityAnimator { target: narrowExtras; to: 0; duration: 160; easing.type: Easing.InCubic }
            YAnimator { target: narrowExtras; to: narrowExtras.restY + 18; duration: 220; easing.type: Easing.InCubic }
            YAnimator { target: numberLayer; to: 0; duration: 260; easing.type: Easing.OutCubic }
            ScaleAnimator { target: numberLayer; to: 1; duration: 260; easing.type: Easing.OutCubic }
            OpacityAnimator { target: expressionText; to: 1; duration: 220 }
        }
        ScriptAction { script: face.applyExtras() }
    }
    property bool menuOpen: false

    // Colors of iPadOS (light / dark)
    readonly property bool dark: Theme.dark
    readonly property color background: dark ? "#000000" : "#ffffff"
    readonly property color sidebarColor: dark ? "#1c1c1e" : "#f2f2f7"
    readonly property color label: dark ? "#ffffff" : "#000000"
    readonly property color secondaryLabel: "#8e8e93"
    readonly property color orange: "#ff9f0a"
    readonly property color separator: dark ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.1)

    readonly property var operators: ["add", "subtract", "multiply", "divide"]
    // The operator waiting for its second number
    readonly property string activeOperator: !Calc.error && operators.indexOf(shell.lastKey) >= 0 ? shell.lastKey : ""
    // "⌫" while a number is being typed, "AC" otherwise
    readonly property bool typing: !Calc.error && Calc.display !== "0"
                                   && ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "decimal", "backspace"].indexOf(shell.lastKey) >= 0

    // Called by the shell (Ctrl+H, --panel)
    function togglePanel(tab) {
        sidebarOpen = !sidebarOpen
    }

    function openPanel(tab) {
        if (tab === "memory") {
            // At the start: the final state at once
            instantExtras = true
            extraOpen = true
            instantExtras = false
        } else
            sidebarOpen = true
    }

    // Escape: closes the menu or the sidebar first
    function closeOverlay() {
        if (menuOpen) {
            menuOpen = false
            return true
        }
        if (sidebarOpen && !wide) {
            sidebarOpen = false
            return true
        }
        return false
    }

    function press(key) {
        shell.press(key)
        shell.refocus()
    }

    Rectangle {
        id: frame

        anchors.fill: parent
        radius: face.cornerRadius
        color: face.background
        border.width: face.shell.maximized ? 0 : 1
        border.color: face.separator
        clip: true

        Behavior on color { ColorAnimation { duration: Theme.durationNormal } }

        // --- Top bar: sidebar button, the "•••" multitasking button, dragging ---
        Item {
            id: topBar

            width: parent.width
            height: 44
            z: 2

            DragHandler {
                target: null
                grabPermissions: PointerHandler.CanTakeOverFromAnything
                onActiveChanged: {
                    if (active)
                        face.shell.startSystemMove()
                }
            }
            TapHandler {
                onDoubleTapped: face.shell.maximized ? face.shell.showNormal() : face.shell.showMaximized()
            }

            GlyphButton {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                iconName: "sidebar-show-symbolic"
                toolTip: qsTr("History (Ctrl+H)")
                onClicked: {
                    face.sidebarOpen = !face.sidebarOpen
                    face.shell.refocus()
                }
            }

            // Multitasking button
            T.AbstractButton {
                id: dots

                anchors.horizontalCenter: parent.horizontalCenter
                y: 6
                width: 48
                height: 22
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Window menu")
                onClicked: face.menuOpen = !face.menuOpen

                background: Rectangle {
                    radius: height / 2
                    color: dots.hovered || face.menuOpen ? Qt.rgba(face.label.r, face.label.g, face.label.b, 0.1) : "transparent"
                }
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent
                        spacing: 4

                        Repeater {
                            model: 3

                            Rectangle {
                                width: 5
                                height: 5
                                radius: 2.5
                                color: face.secondaryLabel
                            }
                        }
                    }
                }
            }
        }

        // Shown while the calculator service is not reachable
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: topBar.height
            z: 3
            width: offlineLabel.implicitWidth + 24
            height: 26
            radius: 13
            color: Qt.rgba(1, 0.27, 0.23, 0.9)
            visible: !Calc.connected

            Text {
                id: offlineLabel

                anchors.centerIn: parent
                text: qsTr("The calculator service is not running")
                color: "white"
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.topMargin: topBar.height
            spacing: 0

            // Wide window: the sidebar takes its place next to the keypad
            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: face.wide && face.sidebarOpen ? 300 : 0
                visible: Layout.preferredWidth > 0
                clip: true

                Behavior on Layout.preferredWidth { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

                HistorySidebar {
                    width: 300
                    height: parent.height
                }
            }

            // --- Calculator ---
            // Nothing here changes its size while an animation runs: the layout changes once, the
            // movement is done by animators (render thread)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 14
                Layout.topMargin: 0
                spacing: 10

                // Display: the previous expression, then the number
                Item {
                    id: display

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 80

                    TapHandler {
                        enabled: face.sidebarOpen && !face.wide
                        onTapped: face.sidebarOpen = false
                    }

                    // Moves up and gets smaller while the extra keys of the narrow layout are shown
                    Item {
                        id: numberLayer

                        width: parent.width
                        height: parent.height
                        transformOrigin: Item.BottomRight

                        Text {
                            id: expressionText

                            anchors.right: parent.right
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: number.height * number.scale
                            anchors.rightMargin: 8
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                            text: Calc.expression
                            color: face.secondaryLabel
                            font.family: Theme.fontFamily
                            font.pixelSize: 24
                        }

                        // Fixed font size (96 px, or 40 px for messages), scaled to the wanted size and
                        // down further when the number is wider than the display
                        Text {
                            id: number

                            readonly property real wanted: Calc.error ? 1 : face.numberSize / 96

                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.rightMargin: 8
                            transformOrigin: Item.BottomRight
                            scale: Math.min(wanted, (display.width - 16) / Math.max(1, implicitWidth))
                            font.pixelSize: Calc.error ? 40 : 96
                            font.family: Theme.fontFamily
                            font.weight: Font.Light
                            color: face.label
                            text: Calc.display
                        }
                    }
                }

                // Keypad area: a fixed height (from the face, not from the layout)
                Item {
                    id: keypadArea

                    readonly property real extraWidth: (width - 10) / 3

                    Layout.fillWidth: true
                    Layout.preferredHeight: (face.height - topBar.height) * 0.64
                    onWidthChanged: face.syncExtras()

                    // Wide: two columns of extra keys on the left (the keypad slides right)
                    Item {
                        id: wideExtras

                        width: keypadArea.extraWidth
                        height: parent.height
                        visible: face.wide && face.extrasLaid
                        opacity: 0

                        GridLayout {
                            anchors.fill: parent
                            flow: GridLayout.TopToBottom
                            rows: 5
                            rowSpacing: 10
                            columnSpacing: 10

                            Repeater {
                                model: face.extraKeys

                                Key {
                                    required property var modelData

                                    key: modelData.key
                                    label: modelData.label
                                    kind: "extra"
                                    labelSize: 19
                                    enabled: Calc.connected && !Calc.error && (modelData.needsMemory !== true || Calc.memory.length > 0)
                                }
                            }
                        }
                    }

                    // --- Keypad (iOS 18 layout) ---
                    Item {
                        id: keypadMover

                        width: face.wide && face.extrasLaid ? parent.width - keypadArea.extraWidth - 10 : parent.width
                        height: parent.height

                        GridLayout {
                            id: keypad

                            anchors.fill: parent
                            columns: 4
                            rowSpacing: 10
                            columnSpacing: 10

                            Key {
                                key: face.typing ? "backspace" : "c"
                                label: face.typing ? "" : "AC"
                                iconName: face.typing ? "edit-clear-symbolic" : ""
                                kind: "function"
                                allowedInError: true
                            }
                            Key { key: "negate"; label: "+/\u2212"; kind: "function" }
                            Key { key: "percent"; label: "%"; kind: "function" }
                            Key { key: "divide"; label: "\u00f7"; kind: "operator" }

                            Key { key: "7"; label: "7"; allowedInError: true }
                            Key { key: "8"; label: "8"; allowedInError: true }
                            Key { key: "9"; label: "9"; allowedInError: true }
                            Key { key: "multiply"; label: "\u00d7"; kind: "operator" }

                            Key { key: "4"; label: "4"; allowedInError: true }
                            Key { key: "5"; label: "5"; allowedInError: true }
                            Key { key: "6"; label: "6"; allowedInError: true }
                            Key { key: "subtract"; label: "\u2212"; kind: "operator" }

                            Key { key: "1"; label: "1"; allowedInError: true }
                            Key { key: "2"; label: "2"; allowedInError: true }
                            Key { key: "3"; label: "3"; allowedInError: true }
                            Key { key: "add"; label: "+"; kind: "operator" }

                            // Calculator key: more functions
                            Key {
                                key: ""
                                calculatorGlyph: true
                                allowedInError: true
                                selected: face.extraOpen
                                Accessible.name: qsTr("More functions")
                                onClicked: {
                                    face.extraOpen = !face.extraOpen
                                    face.shell.refocus()
                                }
                            }
                            Key { key: "0"; label: "0"; allowedInError: true }
                            Key { key: "decimal"; label: face.shell.decimalPoint; allowedInError: true }
                            Key { key: "equals"; label: "="; kind: "operator"; allowedInError: true }
                        }
                    }

                    // Narrow: two rows of extra keys above the keypad, over the bottom of the display
                    Item {
                        id: narrowExtras

                        readonly property real restY: -height - 10

                        width: parent.width
                        height: 88
                        y: restY
                        visible: !face.wide && face.extrasLaid
                        opacity: 0

                        GridLayout {
                            anchors.fill: parent
                            columns: 5
                            rowSpacing: 8
                            columnSpacing: 8

                            Repeater {
                                model: face.extraKeys

                                Key {
                                    required property var modelData

                                    key: modelData.key
                                    label: modelData.label
                                    kind: "extra"
                                    labelSize: 17
                                    enabled: Calc.connected && !Calc.error && (modelData.needsMemory !== true || Calc.memory.length > 0)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Narrow window: the sidebar slides over the calculator
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: topBar.height
            color: Qt.rgba(0, 0, 0, 0.3)
            opacity: face.sidebarOpen && !face.wide ? 1 : 0
            visible: opacity > 0

            Behavior on opacity { NumberAnimation { duration: 220 } }

            TapHandler { onTapped: face.sidebarOpen = false }
        }

        HistorySidebar {
            visible: !face.wide && (face.sidebarOpen || x > -width)
            x: face.sidebarOpen && !face.wide ? 0 : -width
            y: topBar.height
            width: Math.min(300, face.width - 40)
            height: parent.height - topBar.height

            Behavior on x { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        }

        // --- The "•••" menu ---
        MouseArea {
            anchors.fill: parent
            z: 9
            visible: face.menuOpen
            onPressed: face.menuOpen = false
        }

        Rectangle {
            id: menu

            anchors.horizontalCenter: parent.horizontalCenter
            y: topBar.height - 6
            z: 10
            width: 232
            height: menuColumn.implicitHeight + 12
            radius: 13
            color: face.dark ? Qt.rgba(0.17, 0.17, 0.18, 0.98) : Qt.rgba(0.98, 0.98, 0.99, 0.98)
            border.width: 1
            border.color: face.separator
            opacity: face.menuOpen ? 1 : 0
            scale: face.menuOpen ? 1 : 0.92
            transformOrigin: Item.Top
            visible: opacity > 0

            Behavior on opacity { NumberAnimation { duration: 160 } }
            Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

            Column {
                id: menuColumn

                x: 6
                y: 6
                width: parent.width - 12

                MenuRow {
                    text: face.shell.maximized ? qsTr("Exit Full Screen") : qsTr("Full Screen")
                    onClicked: face.shell.maximized ? face.shell.showNormal() : face.shell.showMaximized()
                }
                MenuRow {
                    text: qsTr("Minimize")
                    onClicked: face.shell.showMinimized()
                }
                MenuRow {
                    text: qsTr("Dark Appearance")
                    checked: face.dark
                    onClicked: Theme.setDark(!Theme.dark)
                }
                MenuRow {
                    text: qsTr("Lumen Look")
                    onClicked: face.shell.switchFace("lumen")
                }
                MenuRow {
                    text: qsTr("Cyberpunk Look (Ctrl+Shift+F)")
                    onClicked: face.shell.switchFace("cyber")
                }
                Rectangle {
                    width: parent.width
                    height: 1
                    color: face.separator
                }
                MenuRow {
                    text: Calc.lastCallMs > 0 ? qsTr("Last key over IPC: %1 ms").arg(Calc.lastCallMs.toFixed(2)) : qsTr("IPC: no keys yet")
                    enabled: false
                }
                Rectangle {
                    width: parent.width
                    height: 1
                    color: face.separator
                }
                MenuRow {
                    text: qsTr("Close")
                    destructive: true
                    onClicked: face.shell.close()
                }
            }
        }
    }

    readonly property var extraKeys: [
        { key: "mc", label: "mc", needsMemory: true },
        { key: "mr", label: "mr", needsMemory: true },
        { key: "m+", label: "m+" },
        { key: "m-", label: "m−" },
        { key: "ms", label: "ms" },
        { key: "ce", label: "CE" },
        { key: "reciprocal", label: "1/x" },
        { key: "square", label: "x²" },
        { key: "sqrt", label: "²√x" },
        { key: "c", label: "AC" },
    ]

    // ========================================================================
    // Building blocks
    // ========================================================================

    // Round key. kind: "digit", "function" (light gray), "operator" (orange), "extra" (dark)
    component Key: T.AbstractButton {
        id: button

        property string key
        property string kind: "digit"
        property string label
        property string iconName
        property bool calculatorGlyph: false
        property bool selected: false
        property bool allowedInError: false
        // Fixed font size (extra keys); 0: designed for a key of 96 px and scaled with the key
        property int labelSize: 0
        readonly property bool held: pressed || (key.length > 0 && face.shell.flashKey === key)
        readonly property bool active: kind === "operator" && key === face.activeOperator

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: 1
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        enabled: Calc.connected && (!Calc.error || allowedInError)
        opacity: enabled ? 1 : 0.35
        Accessible.role: Accessible.Button
        Accessible.name: label

        onClicked: {
            if (key.length > 0)
                face.press(key)
        }

        function pick(normal, hover, down) {
            return held ? down : hovered ? hover : normal
        }

        readonly property color fill: {
            if (active)
                return face.dark ? "#ffffff" : "#fcd9a8"
            switch (kind) {
            case "operator": return pick(face.orange, "#ffad33", "#fcc78d")
            case "function": return face.dark ? pick("#a5a5a5", "#b8b8b8", "#d9d9d9") : pick("#d4d4d9", "#cacacf", "#b4b4b9")
            case "extra": return face.dark ? pick("#212121", "#2c2c2c", "#505050") : pick("#f2f2f5", "#e8e8ec", "#d6d6db")
            default:
                if (selected)
                    return face.dark ? "#4d4d4d" : "#d4d4d9"
                return face.dark ? pick("#333333", "#3d3d3d", "#737373") : pick("#e9e9ee", "#e0e0e5", "#c8c8cd")
            }
        }
        readonly property color ink: active ? face.orange
                                    : kind === "operator" ? "#ffffff"
                                    : kind === "function" ? "#000000"
                                    : face.label

        background: Rectangle {
            // Circles in a square cell, capsules in a wide one
            radius: Math.min(width, height) / 2
            color: button.fill

            // Pressing lights the key at once, releasing fades it back slowly (as on iOS)
            Behavior on color { ColorAnimation { duration: button.held ? 0 : 260 } }
        }

        contentItem: Item {
            Text {
                anchors.centerIn: parent
                visible: button.label.length > 0 && !button.calculatorGlyph
                text: button.label
                color: button.ink
                font.family: Theme.fontFamily
                font.pixelSize: button.labelSize > 0 ? button.labelSize
                              : button.kind === "operator" ? 44 : button.kind === "function" ? 35 : 40
                font.weight: button.kind === "digit" ? Font.Normal : Font.Medium
                scale: button.labelSize > 0 ? 1 : face.keyScale

                Behavior on color { ColorAnimation { duration: 180 } }
            }

            LIcon {
                anchors.centerIn: parent
                visible: button.iconName.length > 0
                iconName: button.iconName
                size: 34
                scale: face.keyScale
                tint: button.ink
            }

            // Calculator glyph: a rounded body with a display and keys
            Rectangle {
                anchors.centerIn: parent
                visible: button.calculatorGlyph
                width: Math.round(face.keyUnit * 0.3)
                height: Math.round(width * 1.3)
                radius: width * 0.18
                color: "transparent"
                border.width: Math.max(1.5, width * 0.09)
                border.color: button.ink

                Rectangle {
                    x: parent.width * 0.2
                    y: parent.height * 0.16
                    width: parent.width * 0.6
                    height: parent.height * 0.16
                    radius: 1
                    color: button.ink
                }
                Grid {
                    x: parent.width * 0.2
                    y: parent.height * 0.44
                    columns: 2
                    columnSpacing: parent.width * 0.2
                    rowSpacing: parent.height * 0.1

                    Repeater {
                        model: 4

                        Rectangle {
                            width: Math.max(2, Math.round(face.keyUnit * 0.3) * 0.2)
                            height: width
                            radius: width / 2
                            color: button.ink
                        }
                    }
                }
            }
        }
    }

    // History on the left: expressions in gray, results large; a tap puts a result back
    component HistorySidebar: Rectangle {
        id: sidebar

        color: face.sidebarColor

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: face.separator
        }

        Text {
            id: sidebarTitle

            x: 20
            y: 8
            text: qsTr("History")
            color: face.label
            font.family: Theme.fontFamily
            font.pixelSize: 28
            font.weight: Font.Bold
        }

        T.AbstractButton {
            id: clearButton

            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: sidebarTitle.verticalCenter
            implicitWidth: implicitContentWidth
            implicitHeight: implicitContentHeight
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            enabled: Calc.history.length > 0
            opacity: enabled ? (hovered ? 0.7 : 1) : 0.35
            onClicked: Calc.clearHistory()

            contentItem: Text {
                text: qsTr("Clear")
                color: face.orange
                font.family: Theme.fontFamily
                font.pixelSize: 16
            }
        }

        ListView {
            id: list

            anchors.top: sidebarTitle.bottom
            anchors.topMargin: 8
            anchors.bottom: parent.bottom
            width: parent.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Calc.history

            WheelScroller { parent: list; flickable: list }
            T.ScrollBar.vertical: LScrollBar {}

            delegate: T.AbstractButton {
                id: entry

                required property var modelData

                width: list.width
                height: 76
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onClicked: {
                    Calc.recallHistory(modelData.id)
                    face.shell.noteKey("history")
                    if (!face.wide)
                        face.sidebarOpen = false
                    face.shell.refocus()
                }

                background: Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    radius: 10
                    color: entry.pressed ? Qt.rgba(face.label.r, face.label.g, face.label.b, 0.12)
                         : entry.hovered ? Qt.rgba(face.label.r, face.label.g, face.label.b, 0.06)
                         : "transparent"
                }

                contentItem: Item {
                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: 20
                        anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideLeft
                            text: entry.modelData.expression
                            color: face.secondaryLabel
                            font.family: Theme.fontFamily
                            font.pixelSize: 15
                        }
                        Text {
                            width: parent.width
                            height: 36
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            // Long results get smaller instead of being cut
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 14
                            text: entry.modelData.result
                            color: face.label
                            font.family: Theme.fontFamily
                            font.pixelSize: 28
                            font.weight: Font.Light
                        }
                    }
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.4
            visible: Calc.history.length === 0
            text: qsTr("No History")
            color: face.secondaryLabel
            font.family: Theme.fontFamily
            font.pixelSize: 17
        }
    }

    // Round tinted glyph button of the top bar
    component GlyphButton: T.AbstractButton {
        id: glyph

        property string iconName
        property string toolTip

        width: 34
        height: 34
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.name: toolTip

        background: Rectangle {
            radius: width / 2
            color: glyph.pressed ? Qt.rgba(face.orange.r, face.orange.g, face.orange.b, 0.25)
                 : glyph.hovered ? Qt.rgba(face.orange.r, face.orange.g, face.orange.b, 0.12)
                 : "transparent"
        }
        contentItem: Item {
            LIcon {
                anchors.centerIn: parent
                iconName: glyph.iconName
                size: 18
                tint: face.orange
            }
        }

        LToolTip {
            text: glyph.toolTip
            visible: glyph.hovered && !glyph.pressed && glyph.toolTip.length > 0
        }
    }

    // Row of the "•••" menu
    component MenuRow: T.AbstractButton {
        id: row

        // "checked" (the check mark) is the property of AbstractButton
        property bool destructive: false

        width: parent ? parent.width : 0
        height: 38
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        onClicked: {
            face.menuOpen = false
            face.shell.refocus()
        }

        background: Rectangle {
            radius: 8
            color: row.hovered && row.enabled ? Qt.rgba(face.label.r, face.label.g, face.label.b, 0.08) : "transparent"
        }

        contentItem: Item {
            Text {
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                text: row.text
                color: row.destructive ? "#ff453a" : row.enabled ? face.label : face.secondaryLabel
                font.family: Theme.fontFamily
                font.pixelSize: 15
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                visible: row.checked
                text: "✓"
                color: face.orange
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.Bold
            }
        }
    }
}
