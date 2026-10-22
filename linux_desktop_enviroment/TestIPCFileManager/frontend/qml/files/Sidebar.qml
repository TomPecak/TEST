pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T

import TestFiles

// Navigation pane: Home, the folders of Quick access (XDG folders from the service), This PC
// with the home folder, the file system and the drives (expandable), and the Recycle Bin. The current place is highlighted;
// the middle button opens a place in a new tab, the right button shows its menu.
Item {
    id: sidebar

    required property var explorer
    readonly property var tab: explorer.currentTab
    property bool pcExpanded: true

    Flickable {
        id: flickable

        WheelScroller { parent: flickable; flickable: flickable }

        anchors.fill: parent
        contentHeight: column.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: LScrollBar {}

        Column {
            id: column

            width: flickable.width
            topPadding: Theme.spacingS
            bottomPadding: Theme.spacingS

            NavItem { path: "start"; label: qsTr("Home") }

            NavDivider {}

            Repeater {
                model: Files.quickAccess

                delegate: NavItem {
                    required property string modelData

                    path: modelData
                    pinned: true
                }
            }

            NavDivider {}

            NavItem {
                path: "computer"
                expandable: true
                expanded: sidebar.pcExpanded
                onToggleRequested: sidebar.pcExpanded = !sidebar.pcExpanded
            }

            // Content of This PC, slides open and closed
            Item {
                width: parent.width
                height: sidebar.pcExpanded ? pcColumn.height : 0
                clip: true

                Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingStandard } }

                Column {
                    id: pcColumn

                    width: parent.width
                    opacity: sidebar.pcExpanded ? 1 : 0

                    Behavior on opacity { OpacityAnimator { duration: Theme.durationNormal } }

                    NavItem { path: Files.homePath; depth: 1; visible: path.length > 0 }
                    NavItem { path: "/"; depth: 1 }
                    Repeater {
                        model: Files.drives.map(drive => drive.mountPoint).filter(path => path !== "/")

                        delegate: NavItem {
                            required property string modelData

                            path: modelData
                            depth: 1
                        }
                    }
                }
            }

            NavItem { path: Files.trashPath; visible: path.length > 0 }
        }
    }

    // --- Building blocks ---

    component NavItem: T.AbstractButton {
        id: item

        property string path
        property string label: Files.revision >= 0 ? Files.displayName(path) : ""
        property int depth: 0
        property bool pinned: false
        property bool expandable: false
        property bool expanded: false
        readonly property bool current: sidebar.tab && !sidebar.tab.searching && sidebar.tab.path === path

        signal toggleRequested()

        width: parent ? parent.width : 0
        height: visible ? 32 : 0
        hoverEnabled: true
        leftInset: Theme.spacingXS
        rightInset: Theme.spacingXS
        Accessible.role: Accessible.Button
        Accessible.name: label
        onClicked: sidebar.tab.navigate(path)

        TapHandler {
            acceptedButtons: Qt.MiddleButton | Qt.RightButton
            onTapped: (eventPoint, button) => {
                if (button === Qt.MiddleButton) {
                    sidebar.explorer.newTab(item.path)
                } else {
                    const position = item.mapToItem(null, eventPoint.position.x, eventPoint.position.y)
                    sidebar.explorer.popupMenu(sidebar.explorer.placeMenu(item.path, item.pinned), position.x, position.y)
                }
            }
        }

        background: Rectangle {
            radius: Theme.radiusSmall
            color: item.pressed ? Theme.subtlePressed
                 : item.current ? Theme.controlStrong
                 : item.hovered ? Theme.subtleHover
                 : "transparent"

            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            // Accent pill of the current place
            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: item.current ? 16 : 0
                radius: 1.5
                color: Theme.accent

                Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
            }
        }

        contentItem: Item {
            // Expand / collapse arrow
            SubtleButton {
                id: chevron

                x: 4 + item.depth * 16
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                padding: 0
                visible: item.expandable
                Accessible.name: item.expanded ? qsTr("Collapse") : qsTr("Expand")
                onClicked: item.toggleRequested()

                contentItem: Item {
                    LIcon {
                        anchors.centerIn: parent
                        iconName: "go-next-symbolic"
                        size: 10
                        tint: Theme.textSecondary
                        rotation: item.expanded ? 90 : 0

                        Behavior on rotation { RotationAnimator { duration: Theme.durationNormal; easing.type: Theme.easingStandard } }
                    }
                }
            }

            LIcon {
                id: icon

                x: 26 + item.depth * 16
                anchors.verticalCenter: parent.verticalCenter
                iconName: Files.revision >= 0 ? Files.folderIcon(item.path) : ""
                tinted: false
                size: 16
            }

            LText {
                anchors.left: icon.right
                anchors.leftMargin: Theme.spacingS
                anchors.right: pin.visible ? pin.left : parent.right
                anchors.rightMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                text: item.label
                font.weight: item.current ? Font.DemiBold : Font.Normal
            }

            LIcon {
                id: pin

                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                visible: item.pinned
                iconName: "pin-symbolic"
                size: 12
                tint: Theme.textTertiary
                opacity: item.hovered || item.current ? 1 : 0.55
            }
        }
    }

    component NavDivider: Item {
        width: parent ? parent.width : 0
        height: 13

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingL
            anchors.rightMargin: Theme.spacingL
            anchors.verticalCenter: parent.verticalCenter
            height: 1
            color: Theme.divider
        }
    }
}
