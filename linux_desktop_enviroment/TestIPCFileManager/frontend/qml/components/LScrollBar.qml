import QtQuick
import QtQuick.Templates as T

import TestFiles

// Scroll bar of the Lumen UI (attached as T.ScrollBar.vertical to a Flickable / ListView).
//
// Thin while the view is idle, wider under the mouse; the handle can be dragged and a click
// on the track moves by a page. The hit area is wider than the visible bar, so it is easy to grab.
T.ScrollBar {
    id: bar

    readonly property bool expanded: hovered || pressed

    implicitWidth: orientation === Qt.Vertical ? 14 : implicitContentWidth
    implicitHeight: orientation === Qt.Horizontal ? 14 : implicitContentHeight
    padding: 3
    hoverEnabled: true
    // Large folders: the handle never gets too short to grab
    minimumSize: orientation === Qt.Vertical ? Math.min(1, 32 / Math.max(1, height)) : Math.min(1, 32 / Math.max(1, width))
    visible: policy === T.ScrollBar.AlwaysOn || size < 1
    policy: T.ScrollBar.AsNeeded

    contentItem: Item {
        implicitWidth: 8
        implicitHeight: 8

        Rectangle {
            anchors.right: bar.orientation === Qt.Vertical ? parent.right : undefined
            anchors.bottom: bar.orientation === Qt.Horizontal ? parent.bottom : undefined
            width: bar.orientation === Qt.Vertical ? (bar.expanded ? 8 : 3) : parent.width
            height: bar.orientation === Qt.Horizontal ? (bar.expanded ? 8 : 3) : parent.height
            radius: Math.min(width, height) / 2
            color: bar.pressed ? Theme.textSecondary : Theme.textTertiary
            opacity: bar.expanded ? 0.9 : bar.active ? 0.7 : 0.35

            Behavior on width { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingStandard } }
            Behavior on height { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingStandard } }
            Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        }
    }

    // Track, visible under the mouse
    background: Rectangle {
        radius: 7
        color: Theme.subtleHover
        opacity: bar.expanded ? 1 : 0

        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }
}
