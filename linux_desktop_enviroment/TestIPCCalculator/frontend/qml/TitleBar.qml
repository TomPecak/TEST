import QtQuick
import QtQuick.Templates as T

import TestCalculator

// Title bar of the window: the name, dragging (moves the window, double click maximizes)
// and the minimize / maximize / close buttons. The window has no decoration of the compositor.
Item {
    id: bar

    required property Window window
    readonly property bool maximized: window.visibility === Window.Maximized || window.visibility === Window.FullScreen

    implicitHeight: 32

    DragHandler {
        target: null
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        onActiveChanged: {
            if (active)
                bar.window.startSystemMove()
        }
    }
    TapHandler {
        onDoubleTapped: bar.maximized ? bar.window.showNormal() : bar.window.showMaximized()
    }

    LText {
        x: Theme.spacingL
        anchors.verticalCenter: parent.verticalCenter
        text: bar.window.title
        font.pixelSize: Theme.fontCaption
    }

    Row {
        anchors.right: parent.right
        height: parent.height

        WindowButton {
            glyph: "minimize"
            Accessible.name: qsTr("Minimize")
            onClicked: bar.window.showMinimized()
        }
        WindowButton {
            glyph: bar.maximized ? "restore" : "maximize"
            Accessible.name: bar.maximized ? qsTr("Restore") : qsTr("Maximize")
            onClicked: bar.maximized ? bar.window.showNormal() : bar.window.showMaximized()
        }
        WindowButton {
            glyph: "close"
            closeButton: true
            Accessible.name: qsTr("Close")
            onClicked: bar.window.close()
        }
    }

    component WindowButton: T.AbstractButton {
        id: windowButton

        property string glyph
        property bool closeButton: false
        readonly property color glyphColor: closeButton && hovered ? "white" : Theme.textPrimary

        width: 46
        height: bar.height
        hoverEnabled: true
        focusPolicy: Qt.NoFocus

        background: Rectangle {
            color: windowButton.closeButton
                   ? (windowButton.pressed ? Theme.closePressed : windowButton.hovered ? Theme.closeHover : "transparent")
                   : (windowButton.pressed ? Theme.subtlePressed : windowButton.hovered ? Theme.subtleHover : "transparent")
            topRightRadius: windowButton.closeButton && !bar.maximized ? Theme.radiusMedium : 0

            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }

        contentItem: Item {
            Item {
                anchors.centerIn: parent
                width: 10
                height: 10

                Rectangle {
                    visible: windowButton.glyph === "minimize"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10
                    height: 1
                    color: windowButton.glyphColor
                }
                Rectangle {
                    visible: windowButton.glyph === "maximize"
                    anchors.fill: parent
                    radius: 1.5
                    color: "transparent"
                    border.width: 1
                    border.color: windowButton.glyphColor
                }
                Item {
                    anchors.fill: parent
                    visible: windowButton.glyph === "restore"

                    Rectangle {
                        y: 2
                        width: 8
                        height: 8
                        radius: 1.5
                        color: "transparent"
                        border.width: 1
                        border.color: windowButton.glyphColor
                    }
                    Rectangle { x: 2; width: 7; height: 1; color: windowButton.glyphColor }
                    Rectangle { x: 9; width: 1; height: 7; color: windowButton.glyphColor }
                }
                Item {
                    anchors.fill: parent
                    visible: windowButton.glyph === "close"

                    Rectangle {
                        anchors.centerIn: parent
                        width: 13
                        height: 1
                        rotation: 45
                        antialiasing: true
                        color: windowButton.glyphColor
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 13
                        height: 1
                        rotation: -45
                        antialiasing: true
                        color: windowButton.glyphColor
                    }
                }
            }
        }
    }
}
