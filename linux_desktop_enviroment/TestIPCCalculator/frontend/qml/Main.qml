pragma ComponentBehavior: Bound

import QtQuick
import QtCore

import TestCalculator

// Shell of the calculator window: the frameless window, the keyboard, resizing, the selftest and
// the switching between faces:
//   "lumen" - LumenFace.qml, the standard mode of the Windows 11 calculator
//   "ipad"  - IPadFace.qml, the calculator of iPadOS 18
//   "cyber" - CyberFace.qml, neon on black in the style of the Maia cyberpunk mockup (OLED)
//
// A face is only a look. Every face talks to the same calculator session in the service (Calc),
// so switching (button in the face, Ctrl+Shift+F, --face) keeps the number on the display, the
// pending operation, history and memory. The choice is remembered.
Window {
    id: window

    readonly property bool maximized: visibility === Window.Maximized || visibility === Window.FullScreen
    readonly property string decimalPoint: Qt.locale().decimalPoint
    // Key typed on the keyboard: its button flashes
    property string flashKey: ""
    // Last key (keypad or keyboard): the iPad face highlights the pending operator and shows
    // ⌫ instead of AC while a number is typed
    property string lastKey: ""
    // The faces in the order of switching (Ctrl+Shift+F goes to the next one)
    readonly property var faces: ["lumen", "ipad", "cyber"]
    readonly property string face: faces.indexOf(settings.face) >= 0 ? settings.face : "lumen"
    readonly property string followingFace: faces[(faces.indexOf(face) + 1) % faces.length]
    property string nextFace: ""

    width: Calc.startOptions.width || 340
    height: Calc.startOptions.height || 560
    minimumWidth: 320
    minimumHeight: 480
    visible: true
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    title: qsTr("Calculator")

    Settings {
        id: settings

        category: "TestCalculator"

        property string face: "lumen"
    }

    Component.onCompleted: {
        const face = Calc.startOptions.face
        if (faces.indexOf(face) >= 0)
            settings.face = face
        // The face is known now: load it (and only it)
        faceLoader.active = true
        refocus()
    }

    function press(key) {
        Calc.press(key)
        lastKey = key
        flashKey = key
        flashTimer.restart()
    }

    // Some keys are handled by the face (memory list, history), they still count as "last key"
    function noteKey(key) {
        lastKey = key
    }

    function refocus() {
        keyHandler.forceActiveFocus()
    }

    function switchFace(name) {
        if (name === face || switching.running)
            return
        nextFace = name
        switching.start()
    }

    Timer {
        id: flashTimer

        interval: 110
        onTriggered: window.flashKey = ""
    }

    // ========================================================================
    // Faces
    // ========================================================================

    property bool startPanelOpened: false

    Loader {
        id: faceLoader

        anchors.fill: parent
        active: false
        sourceComponent: window.face === "ipad" ? ipadFace : window.face === "cyber" ? cyberFace : lumenFace
        onLoaded: {
            if (Calc.startOptions.panel && !window.startPanelOpened) {
                window.startPanelOpened = true
                item.openPanel(Calc.startOptions.panel)
            }
            window.refocus()
        }
    }

    Component {
        id: lumenFace

        LumenFace { shell: window }
    }

    Component {
        id: ipadFace

        IPadFace { shell: window }
    }

    Component {
        id: cyberFace

        CyberFace { shell: window }
    }

    // --anim-bench extra|sidebar: opens and closes a panel of the iPad face 6 times and measures
    // how busy the GUI thread is while it animates (layout, text, bindings)
    Timer {
        id: animBench

        property int toggles: 0
        property real lastFrame: 0
        property var gaps: []
        property bool measuring: false

        interval: 700
        repeat: true
        running: (Calc.startOptions.animBench || "").length > 0 && faceLoader.item !== null && faceLoader.item.faceName === "ipad"
        onTriggered: {
            if (toggles === 6) {
                stop()
                if (gaps.length === 0) {
                    console.info("[anim-bench] no frames were drawn")
                    Qt.quit()
                    return
                }
                const sorted = gaps.slice().sort((a, b) => a - b)
                const sum = gaps.reduce((a, b) => a + b, 0)
                const busy = gaps.filter(g => g > 16.7).length
                console.info("[anim-bench] GUI thread probe: " + gaps.length + " ticks, median gap "
                             + sorted[Math.floor(sorted.length / 2)].toFixed(1) + " ms, 95% "
                             + sorted[Math.floor(sorted.length * 0.95)].toFixed(1) + " ms, worst "
                             + sorted[sorted.length - 1].toFixed(1) + " ms, gaps over one frame (16.7 ms): " + busy)
                Qt.quit()
                return
            }
            toggles++
            lastFrame = 0
            measuring = true
            if (Calc.startOptions.animBench === "sidebar")
                faceLoader.item.sidebarOpen = !faceLoader.item.sidebarOpen
            else
                faceLoader.item.extraOpen = !faceLoader.item.extraOpen
            stopMeasuring.restart()
        }
    }
    Timer {
        id: stopMeasuring

        interval: 320
        onTriggered: animBench.measuring = false
    }
    // Probe of the GUI thread: ticks every 4 ms; a longer gap means the thread was busy
    // (layout, text, bindings) and the animation could not get a frame
    Timer {
        interval: 4
        repeat: true
        running: animBench.measuring
        onTriggered: {
            const now = Date.now()
            if (animBench.lastFrame > 0) {
                animBench.gaps.push(now - animBench.lastFrame)
                if (now - animBench.lastFrame > 50)
                    console.info("[probe] gap " + (now - animBench.lastFrame) + " ms at resize step " + resizeBench.step
                                 + ", size " + window.width + "x" + window.height
                                 + (faceLoader.item && faceLoader.item.keyUnit !== undefined ? ", key unit " + faceLoader.item.keyUnit : ""))
            }
            animBench.lastFrame = now
        }
    }

    // --resize-bench: changes the size every 8 ms for 2 s (like dragging the corner of the window)
    // while the probe of the GUI thread runs
    Timer {
        id: resizeBench

        property int step: 0

        interval: 8
        repeat: true
        running: Calc.startOptions.resizeBench === true && Calc.connected && faceLoader.item !== null
        onTriggered: {
            if (step === 0) {
                animBench.gaps = []
                animBench.lastFrame = 0
                animBench.measuring = true
            }
            if (step === 250) {
                stop()
                animBench.measuring = false
                const sorted = animBench.gaps.slice().sort((a, b) => a - b)
                console.info("[resize-bench] " + window.face + ": " + sorted.length + " ticks, median gap "
                             + sorted[Math.floor(sorted.length / 2)].toFixed(1) + " ms, 95% "
                             + sorted[Math.floor(sorted.length * 0.95)].toFixed(1) + " ms, worst "
                             + sorted[sorted.length - 1].toFixed(1) + " ms, gaps over one frame: "
                             + sorted.filter(g => g > 16.7).length)
                Qt.quit()
                return
            }
            // A slow loop between small and large, both directions at once
            const t = step / 250 * Math.PI * 2
            window.width = Math.round(560 + 220 * Math.sin(t))
            window.height = Math.round(620 + 100 * Math.sin(t * 1.5))
            step++
        }
    }

    // --keys 1,2,add: keys pressed after the start (demos, screenshots); "extra" opens the extra
    // keys of the iPad face
    Timer {
        interval: 200
        running: (Calc.startOptions.keys || "").length > 0 && Calc.connected
        onTriggered: {
            for (const key of Calc.startOptions.keys.split(",")) {
                // "extra": the calculator key of the iPad face (more functions)
                if (key === "extra" && faceLoader.item && faceLoader.item.faceName === "ipad")
                    faceLoader.item.extraOpen = !faceLoader.item.extraOpen
                else
                    window.press(key)
            }
        }
    }

    // The old face fades out, the new one fades in with a small zoom
    SequentialAnimation {
        id: switching

        OpacityAnimator { target: faceLoader; to: 0; duration: 110; easing.type: Easing.InCubic }
        ScriptAction {
            script: {
                settings.face = window.nextFace
                faceLoader.scale = 0.97
            }
        }
        ParallelAnimation {
            OpacityAnimator { target: faceLoader; to: 1; duration: 200; easing.type: Easing.OutCubic }
            ScaleAnimator { target: faceLoader; to: 1; duration: 280; easing.type: Easing.OutCubic }
        }
    }

    // ========================================================================
    // Keyboard (the same keys as the calculator of Windows, in every face)
    // ========================================================================

    Item {
        id: keyHandler

        focus: true

        Keys.onPressed: event => {
            const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
            const shift = (event.modifiers & Qt.ShiftModifier) !== 0
            const text = event.text
            let key = ""
            if (ctrl && shift && event.key === Qt.Key_F) {
                window.switchFace(window.followingFace)
            } else if (ctrl) {
                switch (event.key) {
                case Qt.Key_C: Calc.copy(); break
                case Qt.Key_V: Calc.paste(); break
                case Qt.Key_M: key = "ms"; break
                case Qt.Key_P: key = "m+"; break
                case Qt.Key_Q: key = "m-"; break
                case Qt.Key_R: key = "mr"; break
                case Qt.Key_L: key = "mc"; break
                case Qt.Key_H:
                    if (faceLoader.item)
                        faceLoader.item.togglePanel("history")
                    break
                default: return
                }
            } else if (text.length === 1 && text >= "0" && text <= "9") {
                key = text
            } else {
                switch (event.key) {
                case Qt.Key_Return:
                case Qt.Key_Enter: key = "equals"; break
                case Qt.Key_Backspace: key = "backspace"; break
                case Qt.Key_Delete: key = "ce"; break
                case Qt.Key_F9: key = "negate"; break
                case Qt.Key_Escape:
                    if (!faceLoader.item || !faceLoader.item.closeOverlay())
                        key = "c"
                    break
                default:
                    switch (text) {
                    case ".":
                    case ",": key = "decimal"; break
                    case "+": key = "add"; break
                    case "-": key = "subtract"; break
                    case "*": key = "multiply"; break
                    case "/": key = "divide"; break
                    case "=": key = "equals"; break
                    case "%": key = "percent"; break
                    case "@": key = "sqrt"; break
                    case "q":
                    case "Q": key = "square"; break
                    case "r":
                    case "R": key = "reciprocal"; break
                    default: return
                    }
                }
            }
            if (key.length > 0)
                window.press(key)
            event.accepted = true
        }
    }

    // ========================================================================
    // Resizing by the edges and corners
    // ========================================================================

    ResizeArea { edges: Qt.LeftEdge; x: 0; y: 12; width: 5; height: window.height - 24 }
    ResizeArea { edges: Qt.RightEdge; x: window.width - 5; y: 12; width: 5; height: window.height - 24 }
    ResizeArea { edges: Qt.TopEdge; x: 12; y: 0; width: window.width - 24; height: 4 }
    ResizeArea { edges: Qt.BottomEdge; x: 12; y: window.height - 5; width: window.width - 24; height: 5 }
    ResizeArea { edges: Qt.TopEdge | Qt.LeftEdge; x: 0; y: 0; width: 12; height: 12 }
    ResizeArea { edges: Qt.TopEdge | Qt.RightEdge; x: window.width - 12; y: 0; width: 12; height: 12 }
    ResizeArea { edges: Qt.BottomEdge | Qt.LeftEdge; x: 0; y: window.height - 12; width: 12; height: 12 }
    ResizeArea { edges: Qt.BottomEdge | Qt.RightEdge; x: window.width - 12; y: window.height - 12; width: 12; height: 12 }

    component ResizeArea: Item {
        id: resizeArea

        property int edges

        visible: !window.maximized
        z: 50

        HoverHandler {
            cursorShape: resizeArea.edges === Qt.LeftEdge || resizeArea.edges === Qt.RightEdge ? Qt.SizeHorCursor
                       : resizeArea.edges === Qt.TopEdge || resizeArea.edges === Qt.BottomEdge ? Qt.SizeVerCursor
                       : resizeArea.edges === (Qt.TopEdge | Qt.LeftEdge) || resizeArea.edges === (Qt.BottomEdge | Qt.RightEdge)
                         ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
        }

        DragHandler {
            target: null
            grabPermissions: PointerHandler.CanTakeOverFromAnything
            onActiveChanged: {
                if (active)
                    window.startSystemResize(resizeArea.edges)
            }
        }
    }

    // ========================================================================
    // --selftest: keys through the service, checks of the display, switching faces
    // ========================================================================

    Timer {
        id: selfTest

        readonly property string dp: window.decimalPoint
        // [keys, expected display, expected expression (optional)]
        readonly property var steps: [
            [["c", "1", "2", "add", "7", "equals"], "19", "12 + 7 ="],
            [["c", "0", "decimal", "1", "add", "0", "decimal", "2", "equals"], "0" + dp + "3"],
            [["c", "2", "add", "3", "multiply", "4", "equals"], "20"],
            [["c", "5", "add", "3", "equals", "equals"], "11", "8 + 3 ="],
            [["c", "9", "sqrt"], "3", "√(9)"],
            [["c", "5", "divide", "0", "equals"], "Cannot divide by zero"],
            [["c", "1", "divide", "3", "equals"], "0" + dp + "3333333333333333"],
            [["c", "4", "2", "ms", "c", "mr"], "42"],
            // Pending operation, then the face changes: the state lives in the service
            [["c", "6", "multiply", "7"], "7", "6 ×"],
        ]
        property int step: 0
        property int failures: 0
        property string switchedFrom: ""

        function check(name, actual, expected) {
            if (actual === expected) {
                console.info("[selftest] ok   " + name + ": " + actual)
            } else {
                failures++
                console.info("[selftest] FAIL " + name + ": got \"" + actual + "\", expected \"" + expected + "\"")
            }
        }

        interval: 300
        repeat: true
        running: Calc.startOptions.selftest === true && Calc.connected
        onTriggered: {
            // The previous step had time to come back from the service
            if (step > 0 && step <= steps.length) {
                const previous = steps[step - 1]
                check("step " + step + " display", Calc.display, previous[1])
                if (previous.length > 2)
                    check("step " + step + " expression", Calc.expression, previous[2])
            }
            if (step < steps.length) {
                for (const key of steps[step][0])
                    window.press(key)
                step++
                return
            }
            if (switchedFrom.length === 0) {
                // Switch the face in the middle of "6 × 7"
                switchedFrom = window.face
                window.switchFace(window.face === "ipad" ? "lumen" : "ipad")
                return
            }
            if (step === steps.length) {
                step++
                check("face switched", window.face !== switchedFrom && faceLoader.item.faceName === window.face, true)
                check("display kept after switch", Calc.display, "7")
                window.press("equals")
                return
            }
            stop()
            check("pending operation kept: 6 × 7 =", Calc.display, "42")
            check("history has items", Calc.history.length >= 7, true)
            check("memory has 42", Calc.memory.indexOf("42") >= 0, true)
            Calc.evaluate("2+3*(4-1)^2", (ok, result, message) => {
                check("evaluate 2+3*(4-1)^2", result, "29")
                Calc.evaluate("1/0", (ok2, result2, message2) => {
                    check("evaluate 1/0", message2, "Cannot divide by zero")
                    console.info("[selftest] done (" + window.face + "), failures: " + failures)
                })
            })
        }
    }
}
