pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import TestFiles

// File Explorer: the window of the Lumen mock-up, connected to a real file service.
//
// The folders come from the service (Rust, Cap'n Proto over a Unix socket) through C++
// models; nothing here touches the disk. Operations (new folder, rename, Recycle Bin,
// copy / move with progress) are asynchronous: the view changes when the service reports
// the change of the folder (inotify), the same way as for changes made by other programs.
//
// It is a normal application window, without the decoration of the compositor: the tab
// strip is the title bar, the edges resize the window.
Window {
    id: explorer

    // --- Tabs ---

    property var tabs: []
    property int currentIndex: 0
    readonly property var currentTab: tabs.length > 0 ? tabs[Math.min(currentIndex, tabs.length - 1)] : null
    // The tab added last (only it animates in the tab strip)
    property var lastAddedTab: null

    // --- State ---

    // { from, names, paths, cut } or null
    property var clipboard: null
    property bool detailsPaneVisible: false
    readonly property bool maximized: visibility === Window.Maximized || visibility === Window.FullScreen

    width: Math.min(1180, Screen.desktopAvailableWidth - 80)
    height: Math.min(720, Screen.desktopAvailableHeight - 80)
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    title: currentTab ? currentTab.title + " — " + qsTr("File Explorer") : qsTr("File Explorer")

    Component.onCompleted: {
        const options = Files.startOptions
        newTab(options.path || "start")
        if (options.view)
            currentTab.viewMode = options.view
        if (options.search)
            currentTab.query = options.search
        detailsPaneVisible = options.details === true
        focusView()
    }

    onActiveChanged: {
        if (!active)
            menuLayer.hide()
    }

    Component {
        id: tabComponent

        ExplorerTab {}
    }

    // ========================================================================
    // Functions used by the parts of the window
    // ========================================================================

    function newTab(path) {
        const tab = tabComponent.createObject(explorer, { path: path || "start" })
        lastAddedTab = tab
        tabs = tabs.concat([tab])
        currentIndex = tabs.length - 1
        Qt.callLater(() => lastAddedTab = null)
    }

    function closeTab(index) {
        if (tabs.length <= 1) {
            close()
            return
        }
        const tab = tabs[index]
        const list = tabs.slice()
        list.splice(index, 1)
        if (currentIndex > index || currentIndex === list.length)
            currentIndex = Math.max(0, currentIndex - 1)
        tabs = list
        tab.destroy()
    }

    function toggleMaximized() {
        if (maximized)
            showNormal()
        else
            showMaximized()
    }

    function focusView() {
        fileView.forceActiveFocus()
    }

    function refresh() {
        currentTab.reload()
        Files.refreshDrives()
        Files.refreshRecent()
    }

    function itemCount(count) {
        return count === 1 ? qsTr("1 item") : qsTr("%1 items").arg(count)
    }

    function selectedCount(count) {
        return count === 1 ? qsTr("1 item selected") : qsTr("%1 items selected").arg(count)
    }

    function showToast(text) {
        toast.show(text)
    }

    // --- Opening ---

    function openEntry(entry, inNewTab) {
        if (!entry)
            return
        if (entry.isDir) {
            if (inNewTab)
                newTab(entry.path)
            else
                currentTab.navigate(entry.path)
            return
        }
        Files.openFile(entry.path)
        showToast(qsTr("Opening “%1”…").arg(entry.name))
    }

    function openSelection(inNewTab) {
        const entries = currentTab.selectedEntries
        if (entries.length === 1)
            openEntry(entries[0], inNewTab)
        else
            entries.filter(entry => entry.isDir).forEach(entry => openEntry(entry, true))
    }

    // Shows the folder of an entry (search results, Recent) with the entry selected
    function openLocation(entry) {
        const tab = currentTab
        if (tab.navigate(entry.location)) {
            tab.pendingSelect = entry.name
            tab.pendingRename = false
        }
    }

    // --- Changes: requests to the file service ---

    function newFolder() {
        const tab = currentTab
        if (!tab.canModify)
            return
        Files.createFolder(tab.path, qsTr("New folder"), (name, error) => {
            if (name.length === 0) {
                showToast(error)
                return
            }
            // Selected and renamed when the service reports the new entry
            tab.pendingSelect = name
            tab.pendingRename = true
            tab.resolvePending()
        })
    }

    function renameSelection() {
        const tab = currentTab
        if (tab.canModify && tab.selected.length === 1)
            tab.renaming = tab.selected[0]
    }

    function finishRename(oldName, newName) {
        const tab = currentTab
        if (tab.renaming !== oldName)
            return
        tab.renaming = ""
        const name = newName.trim()
        if (name === oldName)
            return
        Files.rename(tab.path, oldName, name, (ok, message) => {
            if (!ok) {
                showToast(message)
                return
            }
            tab.pendingSelect = name
            tab.pendingRename = false
            tab.resolvePending()
        })
    }

    function copySelection(cut) {
        const tab = currentTab
        if (tab.selected.length === 0 || (cut && !tab.canModify))
            return
        const entries = tab.selectedEntries
        clipboard = { from: tab.path, names: entries.map(entry => entry.name),
                      paths: entries.map(entry => entry.path), cut: cut }
        const count = entries.length
        showToast(cut ? (count === 1 ? qsTr("Cut 1 item") : qsTr("Cut %1 items").arg(count))
                      : (count === 1 ? qsTr("Copied 1 item") : qsTr("Copied %1 items").arg(count)))
    }

    function paste() {
        const tab = currentTab
        const clip = clipboard
        if (!clip || !tab.canModify)
            return
        if (Files.jobActive) {
            showToast(qsTr("Wait until the current operation finishes"))
            return
        }
        if (clip.cut)
            clipboard = null
        Files.transfer(clip.paths, tab.path, clip.cut, (ok, message) => {
            showToast(ok ? (clip.cut ? qsTr("Moved: %1").arg(message) : qsTr("Done: %1").arg(message)) : message)
        })
    }

    function deleteSelection() {
        const tab = currentTab
        if (tab.selected.length === 0 || !tab.canModify)
            return
        if (Files.jobActive) {
            showToast(qsTr("Wait until the current operation finishes"))
            return
        }
        const paths = tab.selectedEntries.map(entry => entry.path)
        const count = paths.length
        if (clipboard && clipboard.from === tab.path)
            clipboard = null
        tab.clearSelection()
        Files.trash(paths, (ok, message) => {
            if (ok)
                showToast(count === 1 ? qsTr("Moved 1 item to the Recycle Bin") : qsTr("Moved %1 items to the Recycle Bin").arg(count))
            else
                showToast(message)
        })
    }

    // --- View ---

    function sortBy(role) {
        const tab = currentTab
        if (tab.sortRole === role) {
            tab.sortAscending = !tab.sortAscending
        } else {
            tab.sortRole = role
            tab.sortAscending = role === "name" || role === "type"
        }
    }

    function setView(mode) {
        currentTab.viewMode = mode
    }

    // ========================================================================
    // Menus: lists of { text, icon, shortcut, enabled, checked, action } or { separator }
    // ========================================================================

    readonly property var separator: ({ separator: true })

    function newMenu() {
        return [
            { text: qsTr("Folder"), icon: "folder-new-symbolic", shortcut: "Ctrl+Shift+N", action: () => newFolder() },
            separator,
            { text: qsTr("Text document"), icon: "document-new-symbolic", enabled: false }
        ]
    }

    function sortMenu() {
        const tab = currentTab
        const item = (text, role) => ({ text: text, checked: tab.sortRole === role, action: () => {
            tab.sortRole = role
        } })
        return [
            item(qsTr("Name"), "name"),
            item(qsTr("Date modified"), "modified"),
            item(qsTr("Type"), "type"),
            item(qsTr("Size"), "size"),
            separator,
            { text: qsTr("Ascending"), checked: tab.sortAscending, action: () => tab.sortAscending = true },
            { text: qsTr("Descending"), checked: !tab.sortAscending, action: () => tab.sortAscending = false }
        ]
    }

    function viewItems() {
        const tab = currentTab
        return [
            { text: qsTr("Large icons"), checked: tab.viewMode === "large", shortcut: "Ctrl+Shift+2", action: () => setView("large") },
            { text: qsTr("Medium icons"), checked: tab.viewMode === "medium", shortcut: "Ctrl+Shift+3", action: () => setView("medium") },
            { text: qsTr("Details"), checked: tab.viewMode === "details", shortcut: "Ctrl+Shift+6", action: () => setView("details") }
        ]
    }

    function viewMenu() {
        return viewItems().concat([
            separator,
            { text: qsTr("Hidden items"), checked: currentTab.showHidden, shortcut: "Ctrl+H",
              action: () => currentTab.showHidden = !currentTab.showHidden },
            { text: qsTr("Details pane"), checked: detailsPaneVisible, shortcut: "Alt+Shift+P",
              action: () => detailsPaneVisible = !detailsPaneVisible },
            separator,
            { text: qsTr("Dark mode"), checked: Theme.dark, shortcut: "Ctrl+Shift+D",
              action: () => Theme.setDark(!Theme.dark) }
        ])
    }

    function moreMenu() {
        const tab = currentTab
        return [
            { text: qsTr("Select all"), icon: "edit-select-all-symbolic", shortcut: "Ctrl+A", enabled: tab.count > 0, action: () => tab.selectAll() },
            { text: qsTr("Select none"), icon: "edit-select-none-symbolic", enabled: tab.selected.length > 0, action: () => tab.clearSelection() },
            { text: qsTr("Invert selection"), icon: "edit-select-invert-symbolic", enabled: tab.count > 0, action: () => tab.invertSelection() },
            separator,
            { text: qsTr("Options"), icon: "configure-symbolic", enabled: false }
        ]
    }

    // Menu of the selected items
    function itemMenu() {
        const tab = currentTab
        const entries = tab.selectedEntries
        const single = entries.length === 1
        const folder = single && entries[0].isDir
        const items = [
            { text: qsTr("Open"), icon: "document-open-symbolic", shortcut: "Enter", action: () => openSelection(false) }
        ]
        if (folder)
            items.push({ text: qsTr("Open in new tab"), icon: "tab-new-symbolic", action: () => openSelection(true) })
        if (tab.searching && single)
            items.push({ text: qsTr("Open file location"), icon: "folder-open-symbolic", action: () => openLocation(entries[0]) })
        items.push(separator,
            { text: qsTr("Cut"), icon: "edit-cut-symbolic", shortcut: "Ctrl+X", enabled: tab.canModify, action: () => copySelection(true) },
            { text: qsTr("Copy"), icon: "edit-copy-symbolic", shortcut: "Ctrl+C", action: () => copySelection(false) },
            { text: qsTr("Rename"), icon: "edit-rename-symbolic", shortcut: "F2", enabled: single && tab.canModify, action: () => renameSelection() },
            { text: qsTr("Move to the Recycle Bin"), icon: "edit-delete-symbolic",
              shortcut: "Delete", enabled: tab.canModify, action: () => deleteSelection() },
            separator,
            { text: qsTr("Open in Terminal"), icon: "utilities-terminal-symbolic", enabled: folder,
              action: () => Files.openTerminal(entries[0].path) },
            { text: qsTr("Properties"), icon: "document-properties-symbolic", shortcut: "Alt+Enter", action: () => detailsPaneVisible = true })
        return items
    }

    // Menu of the free space in a folder
    function folderMenu() {
        const tab = currentTab
        return viewItems().concat([
            separator,
            { text: qsTr("New folder"), icon: "folder-new-symbolic", shortcut: "Ctrl+Shift+N", enabled: tab.canModify, action: () => newFolder() },
            separator,
            { text: qsTr("Paste"), icon: "edit-paste-symbolic", shortcut: "Ctrl+V", enabled: clipboard !== null && tab.canModify, action: () => paste() },
            { text: qsTr("Refresh"), icon: "view-refresh-symbolic", shortcut: "F5", action: () => refresh() },
            { text: qsTr("Select all"), icon: "edit-select-all-symbolic", shortcut: "Ctrl+A", enabled: tab.count > 0, action: () => tab.selectAll() },
            separator,
            { text: qsTr("Open in Terminal"), icon: "utilities-terminal-symbolic", enabled: tab.isFolder, action: () => Files.openTerminal(tab.path) },
            { text: qsTr("Properties"), icon: "document-properties-symbolic", action: () => { tab.clearSelection(); detailsPaneVisible = true } }
        ])
    }

    // Menu of a place (navigation pane, Quick access, drives)
    function placeMenu(path, pinned) {
        return [
            { text: qsTr("Open"), icon: "document-open-symbolic", action: () => currentTab.navigate(path) },
            { text: qsTr("Open in new tab"), icon: "tab-new-symbolic", action: () => newTab(path) },
            separator,
            { text: pinned ? qsTr("Unpin from Quick access") : qsTr("Pin to Quick access"), icon: "pin-symbolic", enabled: false },
            { text: qsTr("Properties"), icon: "document-properties-symbolic", action: () => {
                currentTab.navigate(path)
                detailsPaneVisible = true
            } }
        ]
    }

    // Menu of a file in the Recent list of the Home page
    function recentMenu(entry) {
        return [
            { text: qsTr("Open"), icon: "document-open-symbolic", action: () => openEntry(entry, false) },
            { text: qsTr("Open file location"), icon: "folder-open-symbolic", action: () => openLocation(entry) },
            separator,
            { text: qsTr("Remove from Recent"), icon: "edit-delete-symbolic", enabled: false }
        ]
    }

    // Subfolders of a breadcrumb segment: asked from the service, the menu opens when they arrive
    function popupSubfolders(path, item) {
        if (path === "computer") {
            const drives = [{ text: Files.displayName("/"), icon: "folder-symbolic", action: () => currentTab.navigate("/") }]
            for (const drive of Files.drives) {
                if (drive.mountPoint !== "/")
                    drives.push({ text: drive.name + " (" + drive.mountPoint + ")", icon: "folder-symbolic",
                                  action: () => currentTab.navigate(drive.mountPoint) })
            }
            popupMenuBelow(drives, item)
            return
        }
        if (!Files.isFolderPath(path)) {
            popupMenuBelow(Files.quickAccess.map(place => ({
                text: Files.displayName(place), icon: "folder-symbolic", action: () => currentTab.navigate(place)
            })), item)
            return
        }
        subfolders.anchorItem = item
        if (subfolders.path === path)
            subfolders.reload()
        else
            subfolders.path = path
    }

    FolderModel {
        id: subfolders

        property Item anchorItem: null

        onLoadingChanged: {
            if (loading || !anchorItem)
                return
            const folder = path
            const items = subfolders.entries(subfolders.names(0, -1)).filter(entry => entry.isDir).map(entry => ({
                text: entry.name, icon: "folder-symbolic", action: () => explorer.currentTab.navigate(entry.path)
            }))
            explorer.popupMenuBelow(items.length > 0 ? items.slice(0, 40) : [{ text: qsTr("No subfolders"), enabled: false }], anchorItem)
            anchorItem = null
            // The menu has a copy, the subscription is not needed any more
            Qt.callLater(() => { if (subfolders.path === folder) subfolders.path = "" })
        }
    }

    function popupMenu(items, x, y) {
        menuLayer.show(items, x, y)
    }

    function popupMenuBelow(items, item) {
        const position = item.mapToItem(null, 0, item.height + 2)
        popupMenu(items, position.x, position.y)
    }

    // The colors of the theme icons follow the light / dark mode
    Binding {
        target: Files
        property: "darkIcons"
        value: Theme.dark
    }

    // --bench: frames shown with an empty folder view during loading (each one is a visible flash)
    property int emptyFrames: 0
    onFrameSwapped: {
        if (Files.startOptions.bench === true && fileView.showsEmptyLoading)
            emptyFrames++
    }

    // --bench: visits the folders one after another, the models log the load times
    Timer {
        property int step: 0
        readonly property var paths: Files.startOptions.benchPaths || []

        interval: 1500
        repeat: true
        running: Files.startOptions.bench === true && Files.connected
        onTriggered: {
            if (step >= paths.length) {
                stop()
                console.info("[bench] empty frames while loading: " + emptyFrames)
                console.info("[bench] done")
                return
            }
            explorer.currentTab.navigate(paths[step++])
        }
    }

    // --selftest: the operations through the same functions as the buttons and menus
    Timer {
        id: selfTest

        property int step: 0
        readonly property string home: Files.homePath
        readonly property var tab: explorer.currentTab

        function log(text) {
            console.info("[selftest] " + (step - 1) + ": " + text)
        }

        interval: 1200
        repeat: true
        running: Files.startOptions.selftest === true && Files.connected
        onTriggered: {
            switch (step++) {
            case 0:
                tab.navigate(home + "/Documents")
                break
            case 1:
                log("Documents: " + tab.count + " items")
                explorer.newFolder()
                break
            case 2:
                log("renaming: \"" + tab.renaming + "\", selected: " + JSON.stringify(tab.selected))
                tab.renaming = tab.renaming || "New folder"
                explorer.finishRename(tab.renaming, "Renamed folder")
                break
            case 3:
                log("after rename, selected: " + JSON.stringify(tab.selected) + ", has: " + (tab.model.indexOf("Renamed folder") >= 0))
                tab.navigate(home + "/Downloads")
                break
            case 4:
                tab.selectName("random-64MB.bin")
                explorer.copySelection(false)
                log("clipboard: " + JSON.stringify(explorer.clipboard.paths))
                tab.navigate(home + "/Documents")
                break
            case 5:
                explorer.paste()
                log("paste started, job active: " + Files.jobActive)
                break
            case 6:
                log("after paste, has copy: " + (tab.model.indexOf("random-64MB.bin") >= 0) + ", job active: " + Files.jobActive)
                explorer.paste()  // second copy gets a unique name
                break
            case 7:
                log("names: " + JSON.stringify(tab.model.names(0, -1)))
                tab.selectNames(["random-64MB.bin", "random-64MB (2).bin", "Renamed folder"])
                log("deleting: " + JSON.stringify(tab.selected))
                explorer.deleteSelection()
                break
            case 8:
                log("after delete: " + JSON.stringify(tab.model.names(0, -1)))
                tab.navigate(Files.trashPath)
                break
            case 9:
                log("Recycle Bin: " + JSON.stringify(tab.model.names(0, -1)) + ", can modify: " + tab.canModify)
                stop()
                console.info("[selftest] done")
                break
            }
        }
    }

    // ========================================================================
    // Keyboard shortcuts
    // ========================================================================

    Shortcut { sequence: "Ctrl+T"; onActivated: explorer.newTab("start") }
    Shortcut { sequence: "Ctrl+W"; onActivated: explorer.closeTab(explorer.currentIndex) }
    Shortcut { sequences: ["Ctrl+Tab"]; onActivated: explorer.currentIndex = (explorer.currentIndex + 1) % explorer.tabs.length }
    Shortcut { sequences: ["Ctrl+Shift+Tab", "Ctrl+Backtab"]; onActivated: explorer.currentIndex = (explorer.currentIndex + explorer.tabs.length - 1) % explorer.tabs.length }
    Shortcut { sequences: ["Alt+Left", "Backspace"]; onActivated: explorer.currentTab.goBack() }
    Shortcut { sequence: "Alt+Right"; onActivated: explorer.currentTab.goForward() }
    Shortcut { sequence: "Alt+Up"; onActivated: explorer.currentTab.goUp() }
    Shortcut { sequences: ["F5", "Ctrl+R"]; onActivated: explorer.refresh() }
    Shortcut { sequences: ["Ctrl+L", "Alt+D", "F4"]; onActivated: navigationBar.editAddress() }
    Shortcut { sequences: ["Ctrl+F", "Ctrl+E", "F3"]; onActivated: navigationBar.focusSearch() }
    Shortcut { sequence: "Ctrl+Shift+N"; onActivated: explorer.newFolder() }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: Theme.setDark(!Theme.dark) }
    Shortcut { sequence: "Ctrl+H"; onActivated: explorer.currentTab.showHidden = !explorer.currentTab.showHidden }
    Shortcut { sequence: "Ctrl+A"; onActivated: explorer.currentTab.selectAll() }
    Shortcut { sequence: "Ctrl+C"; onActivated: explorer.copySelection(false) }
    Shortcut { sequence: "Ctrl+X"; onActivated: explorer.copySelection(true) }
    Shortcut { sequence: "Ctrl+V"; onActivated: explorer.paste() }
    Shortcut { sequence: "Delete"; onActivated: explorer.deleteSelection() }
    Shortcut { sequence: "F2"; onActivated: explorer.renameSelection() }
    Shortcut { sequence: "Alt+Return"; onActivated: explorer.detailsPaneVisible = true }
    Shortcut { sequence: "Alt+Shift+P"; onActivated: explorer.detailsPaneVisible = !explorer.detailsPaneVisible }
    Shortcut { sequence: "Ctrl+Shift+2"; onActivated: explorer.setView("large") }
    Shortcut { sequence: "Ctrl+Shift+3"; onActivated: explorer.setView("medium") }
    Shortcut { sequence: "Ctrl+Shift+6"; onActivated: explorer.setView("details") }
    Shortcut {
        sequence: "Escape"
        // Text fields (address, search, rename) handle Escape themselves
        enabled: menuLayer.open || fileView.activeFocus
        onActivated: {
            if (menuLayer.open)
                menuLayer.hide()
            else
                explorer.currentTab.clearSelection()
        }
    }

    // ========================================================================
    // Layout
    // ========================================================================

    readonly property int cornerRadius: maximized ? 0 : Theme.radiusMedium

    Rectangle {
        id: frame

        anchors.fill: parent
        radius: explorer.cornerRadius
        color: Theme.windowChrome

        Behavior on color { ColorAnimation { duration: Theme.durationNormal } }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: explorer.maximized ? 0 : 1
            spacing: 0

            TabStrip {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                explorer: explorer
            }

            // Toolbars and content, below the tabs
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.windowToolbar
                bottomLeftRadius: Math.max(0, explorer.cornerRadius - 1)
                bottomRightRadius: Math.max(0, explorer.cornerRadius - 1)

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    NavigationBar {
                        id: navigationBar

                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        explorer: explorer
                    }

                    CommandBar {
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        explorer: explorer
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.divider
                    }

                    // Shown while the file service is not reachable (it reconnects every second)
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Files.connected ? 0 : 34
                        visible: Layout.preferredHeight > 0
                        clip: true
                        color: Qt.rgba(Theme.closeHover.r, Theme.closeHover.g, Theme.closeHover.b, 0.18)

                        Behavior on Layout.preferredHeight { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingStandard } }

                        LText {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingL
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("The file service is not running. Waiting for %1…").arg(Files.socketPath)
                            font.pixelSize: Theme.fontCaption + 1
                        }
                    }

                    // Content area
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.windowContent
                        bottomLeftRadius: Math.max(0, explorer.cornerRadius - 1)
                        bottomRightRadius: Math.max(0, explorer.cornerRadius - 1)

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 0

                                Sidebar {
                                    Layout.preferredWidth: 232
                                    Layout.fillHeight: true
                                    explorer: explorer
                                }

                                Rectangle {
                                    Layout.preferredWidth: 1
                                    Layout.fillHeight: true
                                    color: Theme.divider
                                }

                                FileView {
                                    id: fileView

                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    explorer: explorer
                                    focus: true
                                }

                                DetailsPane {
                                    Layout.preferredWidth: explorer.detailsPaneVisible ? 300 : 0
                                    Layout.fillHeight: true
                                    visible: Layout.preferredWidth > 0
                                    explorer: explorer

                                    Behavior on Layout.preferredWidth {
                                        NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingStandard }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.divider
                            }

                            StatusBar {
                                Layout.fillWidth: true
                                Layout.preferredHeight: implicitHeight
                                explorer: explorer
                            }
                        }
                    }
                }
            }
        }

        // Hairline border of the window, above the content
        Rectangle {
            anchors.fill: parent
            radius: explorer.cornerRadius
            color: "transparent"
            border.width: explorer.maximized ? 0 : 1
            border.color: Theme.stroke
        }
    }

    // Back / forward buttons of the mouse, anywhere in the window. Only these buttons are
    // accepted, other clicks and the hover go through to the items below.
    MouseArea {
        anchors.fill: parent
        z: 60
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onPressed: mouse => {
            menuLayer.hide()
            if (mouse.button === Qt.BackButton)
                explorer.currentTab.goBack()
            else
                explorer.currentTab.goForward()
        }
    }

    // --- Resizing by the edges and corners ---

    ResizeArea { edges: Qt.LeftEdge; x: 0; y: 12; width: 5; height: explorer.height - 24 }
    ResizeArea { edges: Qt.RightEdge; x: explorer.width - 5; y: 12; width: 5; height: explorer.height - 24 }
    ResizeArea { edges: Qt.TopEdge; x: 12; y: 0; width: explorer.width - 24; height: 4 }
    ResizeArea { edges: Qt.BottomEdge; x: 12; y: explorer.height - 5; width: explorer.width - 24; height: 5 }
    ResizeArea { edges: Qt.TopEdge | Qt.LeftEdge; x: 0; y: 0; width: 12; height: 12 }
    ResizeArea { edges: Qt.TopEdge | Qt.RightEdge; x: explorer.width - 12; y: 0; width: 12; height: 12 }
    ResizeArea { edges: Qt.BottomEdge | Qt.LeftEdge; x: 0; y: explorer.height - 12; width: 12; height: 12 }
    ResizeArea { edges: Qt.BottomEdge | Qt.RightEdge; x: explorer.width - 12; y: explorer.height - 12; width: 12; height: 12 }

    component ResizeArea: Item {
        id: resizeArea

        property int edges

        visible: !explorer.maximized
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
                    explorer.startSystemResize(resizeArea.edges)
            }
        }
    }

    // --- Menus ---

    Item {
        id: menuLayer

        property var items: []
        property real menuX: 0
        property real menuY: 0
        readonly property bool open: menu.open

        function show(list, x, y) {
            items = list
            menuX = x
            menuY = y
            if (menu.open)
                menu.replay()
            else
                menu.open = true
        }

        function hide() {
            menu.open = false
        }

        // Always visible: the menu controls its own visibility, the area below catches
        // clicks only while the menu is open
        anchors.fill: parent
        z: 100

        // A click outside closes the menu
        MouseArea {
            anchors.fill: parent
            enabled: menu.open
            acceptedButtons: Qt.AllButtons
            onPressed: menuLayer.hide()
        }

        LMenu {
            id: menu

            readonly property bool openUp: menuLayer.menuY + height > menuLayer.height - 8

            implicitWidth: 248
            openUpwards: openUp
            x: Math.max(8, Math.min(menuLayer.menuX, menuLayer.width - width - 8))
            y: openUp ? Math.max(8, menuLayer.menuY - height) : menuLayer.menuY

            Repeater {
                model: menuLayer.items

                delegate: Item {
                    id: menuEntry

                    required property var modelData
                    readonly property bool isSeparator: modelData.separator === true
                    readonly property bool checkable: modelData.checked !== undefined

                    width: menu.width
                    height: isSeparator ? separatorItem.implicitHeight : menuItem.implicitHeight

                    LMenuSeparator {
                        id: separatorItem

                        width: parent.width
                        visible: menuEntry.isSeparator
                    }

                    LMenuItem {
                        id: menuItem

                        width: parent.width
                        visible: !menuEntry.isSeparator
                        text: menuEntry.modelData.text || ""
                        shortcut: menuEntry.modelData.shortcut || ""
                        iconName: menuEntry.checkable ? (menuEntry.modelData.checked ? "checkmark-symbolic" : "")
                                                      : (menuEntry.modelData.icon || "")
                        iconColor: menuEntry.checkable ? Theme.accent : Theme.textPrimary
                        enabled: menuEntry.modelData.enabled !== false
                        onClicked: {
                            menuLayer.hide()
                            if (menuEntry.modelData.action)
                                menuEntry.modelData.action()
                        }
                    }
                }
            }
        }
    }

    // --- Short messages (copied, moved to the Recycle Bin, ...) ---

    Item {
        id: toast

        property string text

        function show(message) {
            text = message
            hideAnimation.stop()
            hideTimer.restart()
            bubble.opacity = 0
            bubble.y = 12
            showAnimation.restart()
        }

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 44
        width: bubble.width
        height: bubble.height
        z: 90

        Rectangle {
            id: bubble

            width: toastLabel.implicitWidth + 2 * Theme.spacingL
            height: 36
            radius: height / 2
            color: Theme.surface
            border.width: 1
            border.color: Theme.stroke
            opacity: 0
            visible: opacity > 0 || showAnimation.running

            LText {
                id: toastLabel

                anchors.centerIn: parent
                text: toast.text
            }
        }

        ParallelAnimation {
            id: showAnimation

            OpacityAnimator { target: bubble; to: 1; duration: Theme.durationNormal; easing.type: Theme.easingEnter }
            YAnimator { target: bubble; to: 0; duration: Theme.durationNormal; easing.type: Theme.easingEnter }
        }

        ParallelAnimation {
            id: hideAnimation

            OpacityAnimator { target: bubble; to: 0; duration: Theme.durationFast; easing.type: Theme.easingExit }
            YAnimator { target: bubble; to: 6; duration: Theme.durationFast; easing.type: Theme.easingExit }
        }

        Timer {
            id: hideTimer

            interval: 2600
            onTriggered: hideAnimation.start()
        }
    }
}
