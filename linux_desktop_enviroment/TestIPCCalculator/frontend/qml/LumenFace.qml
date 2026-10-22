pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import TestCalculator

// The Lumen face of the calculator: the standard mode of the Windows 11 calculator.
//
// Wide window: History / Memory on the right. Narrow window: they slide up over the keypad.
// Everything shown comes from the service (Calc); the shell (Main.qml) owns the window,
// the keyboard and the switching between faces.
Item {
    id: face

    required property var shell
    readonly property string faceName: "lumen"
    readonly property bool wide: width >= 600
    readonly property int cornerRadius: shell.maximized ? 0 : Theme.radiusMedium
    // Narrow layout: the History / Memory drawer
    property bool drawerOpen: false

    onWideChanged: drawerOpen = false

    // Called by the shell (Ctrl+H, --panel)
    function togglePanel(tab) {
        if (wide) {
            sidePanel.tab = tab
            return
        }
        if (drawerOpen && drawerPanel.tab === tab) {
            drawerOpen = false
        } else {
            drawerPanel.tab = tab
            drawerOpen = true
        }
    }

    function openPanel(tab) {
        sidePanel.tab = tab
        drawerPanel.tab = tab
        drawerOpen = true
    }

    // Escape: closes the drawer first
    function closeOverlay() {
        if (!drawerOpen)
            return false
        drawerOpen = false
        return true
    }

    Rectangle {
        id: frame

        anchors.fill: parent
        radius: face.cornerRadius
        color: Theme.windowChrome
        border.width: face.shell.maximized ? 0 : 1
        border.color: Theme.stroke

        Behavior on color { ColorAnimation { duration: Theme.durationNormal } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: face.shell.maximized ? 0 : 1
            spacing: 0

            TitleBar {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                window: face.shell
            }

            // Shown while the calculator service is not reachable (it reconnects every second)
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Calc.connected ? 0 : 30
                visible: Layout.preferredHeight > 0
                clip: true
                color: Qt.rgba(Theme.closeHover.r, Theme.closeHover.g, Theme.closeHover.b, 0.18)

                Behavior on Layout.preferredHeight { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingStandard } }

                LText {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingL
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("The calculator service is not running. Waiting…")
                    font.pixelSize: Theme.fontCaption
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
                        anchors.leftMargin: Theme.spacingXS
                        anchors.rightMargin: Theme.spacingXS
                        anchors.bottomMargin: Theme.spacingXS
                        spacing: 0

                        // Header: mode, IPC time, theme, history
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 44
                            Layout.leftMargin: Theme.spacingM
                            spacing: Theme.spacingS

                            LText {
                                text: qsTr("Standard")
                                font.pixelSize: Theme.fontSubtitle
                                font.weight: Font.DemiBold
                            }

                            // Time of the last key: from sending it to the answer of the service.
                            // It takes the free space and may shrink to nothing (then it is hidden): a row
                            // wider than the window would make the layout wider than the window,
                            // and the right edge of the whole calculator would be cut off.
                            LText {
                                Layout.alignment: Qt.AlignBottom
                                Layout.bottomMargin: 10
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                visible: Calc.lastCallMs > 0
                                elide: Text.ElideNone
                                opacity: width >= implicitWidth ? 1 : 0
                                text: qsTr("IPC %1 ms").arg(Calc.lastCallMs.toFixed(2))
                                font.pixelSize: Theme.fontCaption - 1
                                color: Theme.textTertiary
                            }

                            // Free space when the IPC time is not shown
                            Item {
                                Layout.fillWidth: true
                                visible: Calc.lastCallMs <= 0
                            }

                            // Switches the whole look of the calculator (the state stays in the service)
                            IconButton {
                                text: "iPadOS"
                                toolTip: qsTr("Switch to the iPadOS look (Ctrl+Shift+F)")
                                onClicked: face.shell.switchFace("ipad")
                            }
                            IconButton {
                                iconName: "contrast-symbolic"
                                toolTip: Theme.dark ? qsTr("Light mode") : qsTr("Dark mode")
                                onClicked: Theme.setDark(!Theme.dark)
                            }
                            IconButton {
                                visible: !face.wide
                                iconName: "view-history-symbolic"
                                highlighted: face.drawerOpen
                                toolTip: qsTr("History (Ctrl+H)")
                                onClicked: face.togglePanel("history")
                            }
                        }

                        // --- Display ---
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 116
                            Layout.minimumHeight: 96

                            TapHandler {
                                // A click above the drawer closes it
                                enabled: face.drawerOpen
                                onTapped: face.drawerOpen = false
                            }

                            LText {
                                id: expressionLine

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.spacingM
                                anchors.leftMargin: Theme.spacingM
                                y: 6
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideLeft
                                text: Calc.expression
                                font.pixelSize: Theme.fontBody
                                color: Theme.textSecondary
                            }

                            Text {
                                id: displayText

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Theme.spacingM
                                anchors.rightMargin: Theme.spacingM
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 4
                                height: 64
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignBottom
                                // Long numbers shrink to fit the width
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 18
                                font.pixelSize: Calc.error ? 30 : 46
                                font.family: Theme.fontFamily
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                                text: Calc.display
                            }
                        }

                        // --- Memory keys ---
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            spacing: 0

                            MemoryButton { text: "MC"; key: "mc"; enabled: Calc.memory.length > 0; toolTip: qsTr("Clear all memory (Ctrl+L)") }
                            MemoryButton { text: "MR"; key: "mr"; enabled: Calc.memory.length > 0; toolTip: qsTr("Memory recall (Ctrl+R)") }
                            MemoryButton { text: "M+"; key: "m+"; toolTip: qsTr("Memory add (Ctrl+P)") }
                            MemoryButton { text: "M−"; key: "m-"; toolTip: qsTr("Memory subtract (Ctrl+Q)") }
                            MemoryButton { text: "MS"; key: "ms"; toolTip: qsTr("Memory store (Ctrl+M)") }
                            MemoryButton {
                                visible: !face.wide
                                text: "M"
                                dropdown: true
                                enabled: Calc.memory.length > 0
                                toolTip: qsTr("Memory")
                                onClicked: face.togglePanel("memory")
                            }
                        }

                        // --- Keypad ---
                        GridLayout {
                            id: keypad

                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: Theme.spacingXS
                            columns: 4
                            rowSpacing: 2
                            columnSpacing: 2

                            Key { key: "percent"; label: "%" }
                            Key { key: "ce"; label: "CE"; error: true }
                            Key { key: "c"; label: "C"; error: true }
                            Key { key: "backspace"; iconName: "edit-clear-symbolic"; label: qsTr("Backspace"); error: true }

                            Key { key: "reciprocal"; label: "1/x" }
                            Key { key: "square"; label: "x²" }
                            Key { key: "sqrt"; label: "²√x" }
                            Key { key: "divide"; label: "÷"; labelSize: 22 }

                            Key { key: "7"; label: "7"; kind: "digit"; error: true }
                            Key { key: "8"; label: "8"; kind: "digit"; error: true }
                            Key { key: "9"; label: "9"; kind: "digit"; error: true }
                            Key { key: "multiply"; label: "×"; labelSize: 22 }

                            Key { key: "4"; label: "4"; kind: "digit"; error: true }
                            Key { key: "5"; label: "5"; kind: "digit"; error: true }
                            Key { key: "6"; label: "6"; kind: "digit"; error: true }
                            Key { key: "subtract"; label: "−"; labelSize: 22 }

                            Key { key: "1"; label: "1"; kind: "digit"; error: true }
                            Key { key: "2"; label: "2"; kind: "digit"; error: true }
                            Key { key: "3"; label: "3"; kind: "digit"; error: true }
                            Key { key: "add"; label: "+"; labelSize: 22 }

                            Key { key: "negate"; label: "+/−"; kind: "digit" }
                            Key { key: "0"; label: "0"; kind: "digit"; error: true }
                            Key { key: "decimal"; label: face.shell.decimalPoint; kind: "digit"; error: true }
                            Key { key: "equals"; label: "="; kind: "accent"; labelSize: 22; error: true }
                        }
                    }

                    // --- History / Memory drawer of the narrow layout ---
                    Item {
                        id: drawer

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        // Covers everything below the display
                        height: parent.height - 44 - 116
                        clip: true
                        visible: !face.wide && (face.drawerOpen || drawerAnimation.running)

                        Rectangle {
                            id: drawerSurface

                            width: parent.width
                            height: parent.height
                            y: face.drawerOpen ? 0 : parent.height
                            radius: Theme.radiusMedium
                            color: Theme.windowToolbar
                            border.width: 1
                            border.color: Theme.stroke

                            Behavior on y {
                                NumberAnimation {
                                    id: drawerAnimation

                                    duration: Theme.durationNormal
                                    easing.type: face.drawerOpen ? Theme.easingEnter : Theme.easingExit
                                }
                            }

                            SidePanel {
                                id: drawerPanel

                                anchors.fill: parent
                                onChosen: {
                                    face.drawerOpen = false
                                    face.shell.refocus()
                                }
                            }
                        }
                    }
                }

                // --- History / Memory on the right (wide layout) ---
                SidePanel {
                    id: sidePanel

                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                    visible: face.wide
                    onChosen: face.shell.refocus()
                }
            }
        }
    }

    // ========================================================================
    // Building blocks
    // ========================================================================

    // Key of the keypad; error: still usable while an error is shown (digits, clearing)
    component Key: CalcButton {
        property bool error: false

        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: 1
        enabled: Calc.connected && (!Calc.error || error)
        flash: face.shell.flashKey === key
        onClicked: {
            face.shell.noteKey(key)
            face.shell.refocus()
        }
    }

    component MemoryButton: IconButton {
        property string key

        Layout.fillWidth: true
        Layout.preferredHeight: 32
        padding: 0
        enabled: Calc.connected && !Calc.error
        onClicked: {
            if (key.length > 0) {
                Calc.press(key)
                face.shell.noteKey(key)
            }
            face.shell.refocus()
        }
    }

}
