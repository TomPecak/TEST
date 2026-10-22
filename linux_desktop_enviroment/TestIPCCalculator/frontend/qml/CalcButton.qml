import QtQuick
import QtQuick.Templates as T

import TestCalculator

// Key of the keypad. Three kinds, like the calculator of Windows 11:
//   "digit"    - digits, the sign and the decimal point: the most solid fill
//   "function" - operators and functions: a lighter fill
//   "accent"   - equals: the accent color
// A key typed on the keyboard flashes the button (flash: true for a moment).
T.AbstractButton {
    id: button

    property string key
    property string kind: "function"
    property string label
    property string iconName
    property int labelSize: kind === "digit" ? 20 : 16
    property bool flash: false
    readonly property bool held: pressed || flash
    readonly property bool accent: kind === "accent"

    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1 : 0.4
    Accessible.role: Accessible.Button
    Accessible.name: label

    onClicked: Calc.press(key)

    function fill(normal, hover, pressed) {
        return held ? pressed : hovered ? hover : normal
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: button.accent
               ? button.fill(Theme.accent, Theme.accentHover, Theme.accentPressed)
               : button.kind === "digit"
                 ? (Theme.dark ? button.fill(Qt.rgba(1, 1, 1, 0.0605), Qt.rgba(1, 1, 1, 0.0837), Qt.rgba(1, 1, 1, 0.0326))
                               : button.fill(Qt.rgba(1, 1, 1, 0.7), Qt.rgba(1, 1, 1, 0.5), Qt.rgba(1, 1, 1, 0.3)))
                 : (Theme.dark ? button.fill(Qt.rgba(1, 1, 1, 0.0326), Qt.rgba(1, 1, 1, 0.0605), Qt.rgba(1, 1, 1, 0.02))
                               : button.fill(Qt.rgba(1, 1, 1, 0.42), Qt.rgba(1, 1, 1, 0.7), Qt.rgba(1, 1, 1, 0.25)))
        border.width: 1
        border.color: button.accent ? "transparent" : Qt.rgba(Theme.stroke.r, Theme.stroke.g, Theme.stroke.b, Theme.stroke.a * 0.8)

        Behavior on color { ColorAnimation { duration: 80 } }
    }

    contentItem: Item {
        LText {
            anchors.centerIn: parent
            visible: button.iconName.length === 0
            text: button.label
            font.pixelSize: button.labelSize
            font.weight: button.kind === "digit" ? Font.DemiBold : Font.Normal
            color: button.accent ? Theme.onAccent : Theme.textPrimary
            opacity: button.held && !button.accent ? 0.75 : 1
        }

        LIcon {
            anchors.centerIn: parent
            visible: button.iconName.length > 0
            iconName: button.iconName
            size: 18
            tint: Theme.textPrimary
        }
    }
}
