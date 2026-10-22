import QtQuick

import TestFiles

// Status bar: the number of items, the selection with its size, the time the folder took to
// come through IPC, the progress of a copy / move, and quick switches of the view
Item {
    id: status

    required property var explorer
    readonly property var tab: explorer.currentTab

    implicitHeight: 28

    Row {
        x: Theme.spacingL
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingM

        LText {
            text: !status.tab || !status.tab.isFolder && !status.tab.searching ? ""
                : status.explorer.itemCount(status.tab.count)
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 12
            color: Theme.divider
            visible: selectionLabel.visible
        }

        LText {
            id: selectionLabel

            visible: status.tab && status.tab.selected.length > 0
            text: {
                if (!visible)
                    return ""
                const count = status.explorer.selectedCount(status.tab.selected.length)
                return status.tab.selectedSize > 0 ? count + "   " + Files.formatSize(status.tab.selectedSize) : count
            }
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: 12
            color: Theme.divider
            visible: timingLabel.visible
        }

        // How long the folder took: the first batch (what the user sees first) and all of it
        LText {
            id: timingLabel

            readonly property var model: status.tab ? status.tab.model : null

            visible: model !== null && model.loadMs > 0 && !Files.jobActive
            text: visible ? qsTr("IPC: first %1 ms · all %2 ms").arg(model.firstBatchMs.toFixed(1)).arg(model.loadMs.toFixed(1)) : ""
            font.pixelSize: Theme.fontCaption
            color: Theme.textTertiary
        }
    }

    // Progress of a copy, a move or the Recycle Bin
    Row {
        anchors.centerIn: parent
        spacing: Theme.spacingS
        visible: Files.jobActive

        LText {
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, 260)
            text: Files.jobText
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 160
            height: 4
            radius: 2
            color: Theme.controlStrong

            Rectangle {
                width: parent.width * Files.jobProgress
                height: parent.height
                radius: 2
                color: Theme.accent

                Behavior on width { NumberAnimation { duration: 100 } }
            }
        }

        LText {
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(Files.jobProgress * 100) + "%"
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        ExplorerButton {
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 22
            implicitHeight: 22
            iconName: "window-close-symbolic"
            iconSize: 10
            toolTip: qsTr("Cancel")
            onClicked: Files.cancelJob()
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingS
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        ExplorerButton {
            implicitWidth: 26
            implicitHeight: 24
            iconName: "view-list-details-symbolic"
            iconSize: 14
            toolTip: qsTr("Details")
            highlighted: status.tab && status.tab.viewMode === "details"
            onClicked: status.explorer.setView("details")
        }
        ExplorerButton {
            implicitWidth: 26
            implicitHeight: 24
            iconName: "view-list-icons-symbolic"
            iconSize: 14
            toolTip: qsTr("Large icons")
            highlighted: status.tab && status.tab.viewMode !== "details"
            onClicked: status.explorer.setView("large")
        }
    }
}
