import QtQuick

// Mouse wheel scrolling for a Flickable / ListView / GridView.
//
// Qt Quick scrolls a little per wheel notch and with a "rubbery" feel. Here one notch moves by
// the number of lines set in the system (Qt.styleHints.wheelScrollLines, 3 by default) times
// lineHeight, with a short smooth animation; quick notches add up. Touchpads (pixel deltas)
// scroll 1:1 without animation. Declare it inside the flickable with "parent: <flickable>".
// Only the wheel is taken: clicks and the hover go through to the items below.
MouseArea {
    id: scroller

    required property Flickable flickable
    property real lineHeight: 40

    // Where the animation is heading, so quick notches add up
    property real destination: 0

    anchors.fill: parent
    z: 1000
    acceptedButtons: Qt.NoButton

    function clamp(y) {
        const top = flickable.originY
        const bottom = flickable.originY + Math.max(0, flickable.contentHeight - flickable.height)
        return Math.max(top, Math.min(bottom, y))
    }

    onWheel: wheel => {
        // Ctrl+wheel and horizontal scrolling are left to others
        if ((wheel.modifiers & Qt.ControlModifier) || (wheel.angleDelta.y === 0 && wheel.pixelDelta.y === 0)) {
            wheel.accepted = false
            return
        }
        flickable.cancelFlick()
        if (wheel.pixelDelta.y !== 0) {
            animation.stop()
            flickable.contentY = clamp(flickable.contentY - wheel.pixelDelta.y)
        } else {
            const start = animation.running ? destination : flickable.contentY
            const step = wheel.angleDelta.y / 120 * Qt.styleHints.wheelScrollLines * lineHeight
            destination = clamp(start - step)
            animation.restart()
        }
        wheel.accepted = true
    }

    NumberAnimation {
        id: animation

        target: scroller.flickable
        property: "contentY"
        to: scroller.destination
        duration: 140
        easing.type: Easing.OutCubic
    }
}
