import QtQuick

import TestFiles

// State of one tab of the File Explorer: the shown folder with its history, the view,
// the sorting, the search and the selection.
//
// The content of the folder lives in a C++ model (FolderModel) fed by the file service:
// the first batch is shown at once, then live changes (inotify) update single rows.
// Paths are absolute; "start" is the Home page and "computer" is This PC.
QtObject {
    id: tab

    property string path: "start"
    property var backStack: []
    property var forwardStack: []

    // "details", "large" or "medium"
    property string viewMode: "details"
    // "name", "modified", "type" or "size"
    property string sortRole: "name"
    property bool sortAscending: true
    property bool showHidden: false
    // Search in the current folder and below
    property string query: ""

    // Names of the selected entries and the anchor of a range selection (Shift)
    property var selected: []
    // The same names as an object, so the delegates check their state in O(1)
    readonly property var selectedSet: {
        const set = {}
        for (const name of selected)
            set[name] = true
        return set
    }
    property int anchorIndex: -1
    property int currentIndex: -1
    // Name of the entry being renamed in place
    property string renaming: ""
    // Name to select (and rename) as soon as the service reports it (new folder, rename)
    property string pendingSelect: ""
    property bool pendingRename: false

    readonly property bool isFolder: Files.isFolderPath(path)
    readonly property bool searching: query.length > 0
    // Files.revision: the names and icons depend on the data of the service (home, places, drives)
    readonly property string title: searching ? qsTr("Search results") : Files.revision >= 0 ? Files.displayName(path) : ""
    readonly property string icon: Files.revision >= 0 ? Files.folderIcon(path) : ""
    readonly property bool canGoBack: backStack.length > 0
    readonly property bool canGoForward: forwardStack.length > 0
    readonly property bool canGoUp: Files.revision >= 0 && Files.parentPath(path) !== ""
    readonly property bool inTrash: Files.revision >= 0 && Files.isInTrash(path)
    readonly property bool canModify: isFolder && !searching && !inTrash && model.error.length === 0
    readonly property int count: model.count

    readonly property FolderModel model: FolderModel {
        // Search on the Home page and This PC looks into the home folder
        // (special pages are passed through, so the model knows when the view may switch to them)
        path: tab.isFolder ? tab.path : (tab.searching ? Files.homePath : tab.path)
        query: tab.query
        sortRole: tab.sortRole
        sortAscending: tab.sortAscending
        showHidden: tab.showHidden

        onEntryAdded: name => {
            if (name === tab.pendingSelect)
                tab.resolvePending()
        }
        // Keeps only the names that still exist (after a delete or a rename); selects the
        // awaited entry when it arrives with the content of the folder
        onCountChanged: {
            tab.pruneSelection()
            if (tab.pendingSelect.length > 0)
                tab.resolvePending()
        }
    }

    // Entries of the selection with all their properties (for menus and the details pane)
    readonly property var selectedEntries: model.revision >= 0 ? model.entries(selected) : []
    readonly property real selectedSize: model.revision >= 0 ? model.totalSize(selected) : 0

    // --- Navigation ---

    function navigate(target) {
        if (!target || target.length === 0)
            return false
        if (target !== path || searching) {
            backStack = backStack.concat([path])
            forwardStack = []
            show(target)
        }
        return true
    }

    function goBack() {
        if (!canGoBack)
            return
        forwardStack = forwardStack.concat([path])
        const target = backStack[backStack.length - 1]
        backStack = backStack.slice(0, -1)
        show(target)
    }

    function goForward() {
        if (!canGoForward)
            return
        backStack = backStack.concat([path])
        const target = forwardStack[forwardStack.length - 1]
        forwardStack = forwardStack.slice(0, -1)
        show(target)
    }

    function goUp() {
        if (canGoUp)
            navigate(Files.parentPath(path))
    }

    function show(target) {
        query = ""
        renaming = ""
        pendingSelect = ""
        clearSelection()
        path = target
    }

    function reload() {
        model.reload()
    }

    // --- Selection ---

    function isSelected(name) {
        return selectedSet[name] === true
    }

    // mode: "single", "toggle" (Ctrl) or "range" (Shift)
    function select(index, mode) {
        const entry = model.get(index)
        if (!entry.name)
            return
        if (mode === "toggle") {
            selected = isSelected(entry.name) ? selected.filter(name => name !== entry.name)
                                              : selected.concat([entry.name])
            anchorIndex = index
        } else if (mode === "range" && anchorIndex >= 0) {
            selected = model.names(Math.min(anchorIndex, index), Math.max(anchorIndex, index))
        } else {
            selected = [entry.name]
            anchorIndex = index
        }
        currentIndex = index
    }

    function selectName(name) {
        const index = model.indexOf(name)
        if (index >= 0)
            select(index, "single")
        return index
    }

    function selectNames(names) {
        selected = model.existing(names)
    }

    function selectAll() {
        selected = model.names(0, -1)
    }

    function clearSelection() {
        selected = []
        anchorIndex = -1
        currentIndex = -1
    }

    function invertSelection() {
        const all = model.names(0, -1)
        selected = all.filter(name => !isSelected(name))
    }

    // Selects (and starts renaming) the awaited entry once it is in the model
    function resolvePending() {
        if (pendingSelect.length === 0 || selectName(pendingSelect) < 0)
            return
        if (pendingRename)
            renaming = pendingSelect
        pendingSelect = ""
        pendingRename = false
    }

    function pruneSelection() {
        if (selected.length > 0) {
            const kept = model.existing(selected)
            if (kept.length !== selected.length)
                selected = kept
        }
        if (currentIndex >= model.count)
            currentIndex = model.count - 1
    }
}
