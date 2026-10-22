pragma ComponentBehavior: Bound

import QtQuick

import TestFiles

// Details pane on the right: a large icon, the name, the type and the properties of
// the selected item, of several selected items, or of the current folder.
Item {
    id: pane

    required property var explorer
    readonly property var tab: explorer.currentTab
    readonly property var selection: tab ? tab.selectedEntries : []
    readonly property var entry: selection.length === 1 ? selection[0] : null
    readonly property bool folderInfo: selection.length === 0

    // Properties shown in the list: [{ label, value }]
    readonly property var properties: {
        if (!tab)
            return []
        if (entry) {
            const list = [{ label: qsTr("Type"), value: entry.typeName }]
            if (!entry.isDir)
                list.push({ label: qsTr("Size"), value: Files.formatSize(entry.size) + "  (" + Number(entry.size).toLocaleString(Qt.locale(), "f", 0) + " B)" })
            list.push({ label: qsTr("Date modified"), value: Files.formatDate(entry.modified) })
            list.push({ label: qsTr("MIME type"), value: entry.mimeType })
            if (entry.isSymlink)
                list.push({ label: qsTr("Link"), value: qsTr("Symbolic link") })
            list.push({ label: qsTr("Location"), value: Files.shortPath(entry.location) })
            return list
        }
        if (selection.length > 1)
            return [{ label: qsTr("Total size"), value: Files.formatSize(tab.selectedSize) }]
        const list = [{ label: qsTr("Location"), value: tab.isFolder ? Files.shortPath(tab.path) : Files.toLocation(tab.path) }]
        if (tab.isFolder) {
            list.unshift({ label: qsTr("Items"), value: String(tab.count) })
            if (tab.model.loadMs > 0)
                list.push({ label: qsTr("Loaded through IPC"),
                            value: qsTr("first batch %1 ms, all %2 ms").arg(tab.model.firstBatchMs.toFixed(1)).arg(tab.model.loadMs.toFixed(1)) })
        }
        return list
    }

    clip: true

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.divider
    }

    Column {
        x: Theme.spacingXL
        y: Theme.spacingXL
        width: pane.width - 2 * Theme.spacingXL
        spacing: Theme.spacingM

        // Two icons: switching the source (theme / own symbolic icons) of one icon together
        // with its name would try to load the name from the wrong source for a moment
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 96
            height: 96

            LIcon {
                visible: pane.selection.length <= 1
                iconName: pane.entry ? pane.entry.iconName
                        : pane.tab ? pane.tab.icon : "folder"
                tinted: false
                size: 96
            }

            LIcon {
                visible: pane.selection.length > 1
                iconName: "edit-select-all-symbolic"
                tint: Theme.textSecondary
                size: 96
            }
        }

        LText {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 3
            text: pane.entry ? pane.entry.name
                : pane.selection.length > 1 ? pane.explorer.selectedCount(pane.selection.length)
                : pane.tab ? pane.tab.title : ""
            font.pixelSize: Theme.fontSubtitle - 2
            font.weight: Font.DemiBold
        }

        LText {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: text.length > 0
            text: pane.entry ? pane.entry.typeName : pane.folderInfo && pane.tab && pane.tab.isFolder ? qsTr("Folder") : ""
            color: Theme.textSecondary
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.divider
        }

        LText {
            text: qsTr("Properties")
            font.pixelSize: Theme.fontCaption
            font.weight: Font.DemiBold
            color: Theme.textSecondary
        }

        Repeater {
            model: pane.properties

            delegate: Column {
                id: propertyRow

                required property var modelData

                width: parent ? parent.width : 0
                spacing: 2

                LText {
                    text: propertyRow.modelData.label
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textTertiary
                }
                LText {
                    width: parent.width
                    text: propertyRow.modelData.value
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 2
                }
            }
        }
    }
}
