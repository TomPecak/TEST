pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T

import TestCalculator

// History and Memory, as in the calculator of Windows 11. A click on a history item puts its
// result (and the calculation) back on the display; memory items can be recalled, changed
// with M+ / M− or removed. Both lists live in the service and arrive here live.
Item {
    id: panel

    property string tab: "history"
    // A choice was made (the drawer of the narrow layout closes)
    signal chosen()

    // Tabs
    Row {
        id: tabs

        x: Theme.spacingS
        y: Theme.spacingS
        spacing: Theme.spacingXS

        TabButton { tab: "history"; text: qsTr("History") }
        TabButton { tab: "memory"; text: qsTr("Memory") }
    }

    Item {
        anchors.top: tabs.bottom
        anchors.topMargin: Theme.spacingS
        anchors.bottom: parent.bottom
        width: parent.width

        // --- History ---
        ListView {
            id: historyList

            anchors.fill: parent
            anchors.bottomMargin: 48
            visible: panel.tab === "history"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Calc.history
            spacing: 2

            WheelScroller { parent: historyList; flickable: historyList }
            T.ScrollBar.vertical: LScrollBar {}

            delegate: Entry {
                id: historyEntry

                required property var modelData

                width: historyList.width
                onClicked: {
                    Calc.recallHistory(modelData.id)
                    panel.chosen()
                }

                Column {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingM
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    LText {
                        width: parent.width
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideLeft
                        text: historyEntry.modelData.expression
                        font.pixelSize: Theme.fontCaption + 1
                        color: Theme.textSecondary
                    }
                    LText {
                        width: parent.width
                        height: 28
                        horizontalAlignment: Text.AlignRight
                        // Long results get smaller instead of being cut
                        elide: Text.ElideNone
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 12
                        text: historyEntry.modelData.result
                        font.pixelSize: Theme.fontSubtitle
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        // --- Memory ---
        ListView {
            id: memoryList

            anchors.fill: parent
            anchors.bottomMargin: 48
            visible: panel.tab === "memory"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Calc.memory
            spacing: 2

            WheelScroller { parent: memoryList; flickable: memoryList }
            T.ScrollBar.vertical: LScrollBar {}

            delegate: Entry {
                id: memoryEntry

                required property string modelData
                required property int index

                width: memoryList.width
                onClicked: {
                    Calc.memoryItem(index, "recall")
                    panel.chosen()
                }

                LText {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingM
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingM
                    y: Theme.spacingS
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideLeft
                    text: memoryEntry.modelData
                    font.pixelSize: Theme.fontSubtitle
                    font.weight: Font.DemiBold
                }

                // Item actions, shown under the mouse
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingS
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spacingXS
                    spacing: 2
                    opacity: memoryEntry.hovered ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { OpacityAnimator { duration: Theme.durationFast } }

                    SmallButton { text: "MC"; onClicked: Calc.memoryItem(memoryEntry.index, "clear") }
                    SmallButton { text: "M+"; onClicked: Calc.memoryItem(memoryEntry.index, "add") }
                    SmallButton { text: "M−"; onClicked: Calc.memoryItem(memoryEntry.index, "subtract") }
                }
            }
        }

        LText {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingL
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingL
            y: Theme.spacingS
            wrapMode: Text.Wrap
            visible: panel.tab === "history" ? Calc.history.length === 0 : Calc.memory.length === 0
            text: panel.tab === "history" ? qsTr("There's no history yet") : qsTr("There's nothing saved in memory")
            color: Theme.textSecondary
        }

        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingS
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingS
            iconName: "edit-delete-symbolic"
            toolTip: panel.tab === "history" ? qsTr("Clear all history") : qsTr("Clear all memory")
            enabled: Calc.connected && (panel.tab === "history" ? Calc.history.length > 0 : Calc.memory.length > 0)
            onClicked: panel.tab === "history" ? Calc.clearHistory() : Calc.clearMemory()
        }
    }

    // --- Building blocks ---

    component TabButton: T.AbstractButton {
        id: tabButton

        property string tab
        readonly property bool current: panel.tab === tab

        implicitWidth: label.implicitWidth + 2 * Theme.spacingM
        implicitHeight: 36
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        onClicked: panel.tab = tab

        background: Rectangle {
            radius: Theme.radiusSmall
            color: tabButton.pressed ? Theme.subtlePressed : tabButton.hovered ? Theme.subtleHover : "transparent"

            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            // Accent underline of the current tab
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 3
                width: tabButton.current ? 16 : 0
                height: 3
                radius: 1.5
                color: Theme.accent

                Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
            }
        }

        contentItem: LText {
            id: label

            horizontalAlignment: Text.AlignHCenter
            text: tabButton.text
            font.weight: tabButton.current ? Font.DemiBold : Font.Normal
            color: tabButton.current ? Theme.textPrimary : Theme.textSecondary
        }
    }

    // History / memory item: hover and press fill, clickable
    component Entry: T.AbstractButton {
        implicitHeight: 64
        hoverEnabled: true
        focusPolicy: Qt.NoFocus

        background: Rectangle {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingXS
            anchors.rightMargin: Theme.spacingXS
            radius: Theme.radiusSmall
            color: parent.pressed ? Theme.subtlePressed : parent.hovered ? Theme.subtleHover : "transparent"

            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
    }

    component SmallButton: T.AbstractButton {
        id: small

        implicitWidth: 34
        implicitHeight: 24
        hoverEnabled: true
        focusPolicy: Qt.NoFocus

        background: Rectangle {
            radius: Theme.radiusSmall
            color: small.pressed ? Theme.subtlePressed : small.hovered ? Theme.controlStrong : Theme.control
            border.width: 1
            border.color: Theme.stroke
        }

        contentItem: LText {
            horizontalAlignment: Text.AlignHCenter
            text: small.text
            font.pixelSize: Theme.fontCaption
        }
    }
}
