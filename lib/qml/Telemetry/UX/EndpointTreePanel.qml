// Reusable presentation: the caller owns the provider and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQml.Models

Pane {
    id: panel
    objectName: "endpointTreePane"
    required property var provider
    // Evaluate the provider's O(1) current-index projection once per panel,
    // rather than querying it separately for every visible delegate.
    readonly property var selectedEndpointIndex: provider.selectedEndpointIndex
    readonly property var searchSource: provider.endpoints
    property var searchResults: []
    property var searchStack: []
    property bool searchRunning: false
    property bool searchLimited: false
    property int searchGeneration: 0
    readonly property int searchLimit: 500
    readonly property int searchDepthLimit: 64
    // These are source-model indexes, independent of the endpoint editor.
    // Never retain them across structural changes to the source model.
    property var watchMarks: []
    property int watchMarkGeneration: 0
    property string watchMarkStatus: ""
    property bool enterPending: false
    readonly property int groupWalkLimit: 4096
    readonly property bool bulkWatchAvailable: provider &&
        typeof provider.addWatchMany === "function" &&
        typeof provider.watchCandidate === "function"

    function clearWatchMarks() {
        ++watchMarkGeneration
        watchMarks = []
        watchMarkStatus = ""
        // The open menu holds a plain index too: it never outlives a change.
        if (endpointMenu.visible)
            endpointMenu.close()
        endpointMenu.target = undefined
        endpointMenu.targetIsEndpoint = false
        endpointMenu.targetIsGroup = false
    }
    // The menu's index, or null once a change has dropped it.
    function menuTarget() {
        const target = endpointMenu.target
        return target && target.valid ? target : null
    }
    function canMarkWatch(index) {
        return Boolean(bulkWatchAvailable && index && index.valid &&
                       provider.watchCandidate(index))
    }
    function isWatchMarked(index) {
        return watchMarks.some(function(item) { return item === index })
    }
    function toggleWatchMark(index) {
        if (!canMarkWatch(index)) return
        const marks = watchMarks.slice()
        const position = marks.findIndex(function(item) { return item === index })
        if (position < 0) marks.push(index)
        else marks.splice(position, 1)
        ++watchMarkGeneration
        watchMarks = marks
        watchMarkStatus = ""
    }
    function addMarkedWatches() {
        if (!bulkWatchAvailable || watchMarks.length === 0 || provider.watchActive === true) return
        const generation = watchMarkGeneration
        const originalProvider = provider
        const originalSource = searchSource
        const result = originalProvider.addWatchMany(watchMarks.slice())
        if (generation !== watchMarkGeneration || provider !== originalProvider || searchSource !== originalSource)
            return
        if (!result || result.status !== "ok") {
            watchMarkStatus = "Could not add fields: " + (result && result.status ? result.status : "invalid")
            return
        }
        const summary = "Added " + result.added + "; already watched " + result.duplicate +
                        "; unsupported " + result.unsupported + "; capacity " + result.capacity
        clearWatchMarks()
        watchMarkStatus = summary
    }

    // The name of one source index, read through its named role like search.
    function nameOf(index) {
        if (!index || !index.valid || !searchSource)
            return ""
        nameModel.rootIndex = index.parent
        if (index.row < 0 || index.row >= nameModel.count)
            return ""
        const item = nameModel.items.get(index.row)
        return item && item.model && item.model.name !== undefined ? String(item.model.name) : ""
    }
    // Dotted path from the root, as typed into the search: "measure.Voltage".
    function pathOf(index) {
        const parts = []
        for (let i = index; i && i.valid && parts.length < searchDepthLimit; i = i.parent)
            parts.unshift(nameOf(i))
        return parts.join(".")
    }
    // Every Field under a group that the Watchlist accepts, in tree order.
    // The walk is bounded like search: depth 64 and 4096 visited rows; a
    // walk cut short says so.
    function groupCandidates(group) {
        const found = []
        let budget = groupWalkLimit
        const walk = function(parent, depth) {
            const count = searchSource.rowCount(parent)
            for (let row = 0; row < count && budget > 0; ++row) {
                const child = searchSource.index(row, 0, parent)
                --budget
                if (canMarkWatch(child))
                    found.push(child)
                if (depth + 1 < searchDepthLimit && searchSource.rowCount(child) > 0)
                    walk(child, depth + 1)
            }
        }
        if (searchSource && group && group.valid)
            walk(group, 0)
        return { indexes: found, limited: budget <= 0 }
    }
    // Adds Fields straight to the Watchlist, apart from the ticked marks.
    function watchIndexes(indexes, label, group, limited) {
        if (!bulkWatchAvailable)
            return false
        if (provider.watchActive === true) {
            watchMarkStatus = "Stop polling to change the Watchlist."
            return false
        }
        if (indexes.length === 0) {
            watchMarkStatus = "Nothing in " + label + " can be watched."
            return false
        }
        const originalProvider = provider
        const result = originalProvider.addWatchMany(indexes)
        if (provider !== originalProvider)
            return false
        if (!result || result.status !== "ok") {
            watchMarkStatus = "Could not watch " + label + ": " + (result && result.status ? result.status : "invalid")
            return false
        }
        if (!group)
            watchMarkStatus = result.added === 1 ? "Watching " + label + "." :
                              result.duplicate === 1 ? label + " is already watched." :
                              result.capacity === 1 ? "The Watchlist is full; " + label + " was not added." :
                              label + " cannot be watched."
        else
            watchMarkStatus = "Watching " + result.added + " from " + label + "; already watched " + result.duplicate +
                              "; unsupported " + result.unsupported + "; capacity " + result.capacity +
                              (limited ? "; stopped after " + groupWalkLimit + " rows" : "")
        return result.added > 0
    }
    function watchIndex(index) {
        return canMarkWatch(index) ? watchIndexes([index], pathOf(index), false, false) : false
    }
    function watchGroup(index) {
        const walk = groupCandidates(index)
        return watchIndexes(walk.indexes, pathOf(index), true, walk.limited)
    }
    function copyPath(index) {
        const path = pathOf(index)
        if (path.length === 0)
            return false
        clipboardHelper.text = path
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.text = ""
        watchMarkStatus = "Copied " + path + "."
        return true
    }
    function isGroup(index) {
        return !!searchSource && !!index && index.valid && searchSource.rowCount(index) > 0
    }
    // Opens the actions of one row next to the item it was opened from.
    function openEndpointMenu(index, isEndpoint, anchor, x, y) {
        if (!index || !index.valid || !anchor)
            return false
        endpointMenu.target = index
        endpointMenu.targetIsEndpoint = isEndpoint
        endpointMenu.targetIsGroup = isGroup(index)
        endpointMenu.returnFocus = panel.Window.activeFocusItem
        endpointMenu.popup(anchor, x === undefined ? 16 : x, y === undefined ? anchor.height : y)
        return true
    }
    // For a host's Ctrl+K: focus the search, optionally with a query.
    function find(text) {
        if (text !== undefined)
            searchInput.text = String(text)
        searchInput.forceActiveFocus()
        searchInput.selectAll()
    }
    // Enter in the search watches the first result; a Command, a Service or a
    // Field the Watchlist cannot hold opens its details instead. Returns
    // whether there was a result to act on.
    function activateFirstResult() {
        if (searchRunning || searchResults.length === 0)
            return false
        const first = searchResults[0]
        if (first.generation !== searchGeneration || !first.index.valid)
            return false
        if (canMarkWatch(first.index))
            watchIndex(first.index)
        else
            provider.selectEndpoint(first.index)
        return true
    }
    // Enter before the search has finished is kept and acted on when it does;
    // with nothing to act on, the key goes on to the host (a default button).
    function searchEnter(event) {
        if (searchRunning) {
            enterPending = true
            event.accepted = true
            return
        }
        event.accepted = activateFirstResult()
    }
    // What Enter will do, worded from the first result; empty when nothing.
    function enterHint() {
        if (!bulkWatchAvailable || !searchInput.activeFocus || searchRunning || searchResults.length === 0)
            return ""
        const first = searchResults[0]
        if (!first.index || !first.index.valid)
            return ""
        if (!canMarkWatch(first.index))
            return "; Enter shows the first"
        return provider.watchActive === true ? "" : "; Enter watches the first"
    }

    // DelegateModel exposes named roles independently of their numeric IDs.
    // It does not create visual delegates for this traversal. Results retain
    // source indexes only until the next model change or search cancellation.
    function cancelSearch() {
        enterPending = false
        ++searchGeneration
        searchTimer.stop()
        searchRunning = false
        searchResults = []
        searchStack = []
    }
    function scheduleSearch() {
        cancelSearch()
        searchLimited = false
        if (searchInput.text.trim().length > 0 && searchSource) {
            searchRunning = true
            searchTimer.interval = 50
            searchTimer.restart()
        }
    }
    function scanSearch() {
        if (!searchSource || searchInput.text.trim().length === 0) {
            cancelSearch()
            return
        }
        const needle = searchInput.text.trim().toLocaleLowerCase().replace(/\s*\/\s*/g, ".")
        const generation = searchGeneration
        if (searchStack.length === 0)
            searchStack = [{ parent: searchSource.index(-1, -1), row: 0, path: "", depth: 0 }]
        let visited = 0
        let matches = searchResults.slice()
        while (searchStack.length && visited < 128 && matches.length < searchLimit) {
            const frame = searchStack[searchStack.length - 1]
            searchModel.rootIndex = frame.parent
            if (generation !== searchGeneration) return
            if (frame.row >= searchModel.count) {
                searchStack.pop()
                continue
            }
            const row = frame.row++
            const index = searchModel.modelIndex(row)
            const item = searchModel.items.get(row)
            if (generation !== searchGeneration) return
            ++visited
            if (!item || !item.model || !index.valid)
                continue
            const name = String(item.model.name === undefined ? "" : item.model.name)
            const endpoint = item.model.isEndpoint !== undefined && Boolean(item.model.isEndpoint)
            const children = searchSource.rowCount(index)
            if (generation !== searchGeneration) return
            const path = frame.path.length ? frame.path + " / " + name : name
            if (endpoint && path.toLocaleLowerCase().replace(/\s*\/\s*/g, ".").indexOf(needle) !== -1)
                matches.push({ path: path, index: index, generation: generation })
            if (children > 0 && frame.depth + 1 < searchDepthLimit)
                searchStack.push({ parent: index, row: 0, path: path, depth: frame.depth + 1 })
            else if (children > 0)
                searchLimited = true
        }
        searchResults = matches
        if (matches.length >= searchLimit) {
            searchLimited = true
            searchStack = []
        }
        if (searchStack.length) {
            searchTimer.interval = 0
            searchTimer.restart()
        } else {
            searchRunning = false
            if (enterPending) {
                enterPending = false
                activateFirstResult()
            }
        }
    }
    onProviderChanged: clearWatchMarks()
    onSearchSourceChanged: { clearWatchMarks(); scheduleSearch() }
    DelegateModel {
        id: searchModel
        model: panel.searchSource
        delegate: Item {}
    }
    DelegateModel {
        id: nameModel
        model: panel.searchSource
        delegate: Item {}
    }
    Menu {
        id: endpointMenu
        objectName: "endpointContextMenu"
        property var target
        property bool targetIsEndpoint: false
        property bool targetIsGroup: false
        // Qt 6.4 does not hand focus back when a Menu closes; the keyboard
        // returns to whatever had it when the menu opened.
        property Item returnFocus: null
        onClosed: {
            const item = returnFocus
            returnFocus = null
            if (item && item.visible && !item.activeFocus)
                item.forceActiveFocus()
        }
        MenuItem {
            objectName: "endpointMenuWatch"
            text: "Watch"
            enabled: panel.provider.watchActive !== true && panel.canMarkWatch(endpointMenu.target)
            onTriggered: { const target = panel.menuTarget(); if (target) panel.watchIndex(target) }
        }
        MenuItem {
            objectName: "endpointMenuWatchGroup"
            text: "Watch this group"
            enabled: panel.bulkWatchAvailable && endpointMenu.targetIsGroup && panel.provider.watchActive !== true
            onTriggered: { const target = panel.menuTarget(); if (target) panel.watchGroup(target) }
        }
        MenuItem {
            objectName: "endpointMenuDetails"
            text: "Show details"
            enabled: endpointMenu.targetIsEndpoint
            onTriggered: { const target = panel.menuTarget(); if (target) panel.provider.selectEndpoint(target) }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "endpointMenuCopyPath"
            text: "Copy path"
            onTriggered: { const target = panel.menuTarget(); if (target) panel.copyPath(target) }
        }
    }
    Timer { id: searchTimer; repeat: false; onTriggered: panel.scanSearch() }
    palette.window: "#11151c"
    palette.windowText: "#e8edf5"
    palette.base: "#17202b"
    palette.alternateBase: "#1c2836"
    palette.text: "#e8edf5"
    palette.button: "#233349"
    palette.buttonText: "#e8edf5"
    palette.highlight: "#215a68"
    palette.highlightedText: "#ffffff"
    palette.placeholderText: "#718297"
    background: Rectangle { objectName: "endpointTreePanelBackground"; color: panel.palette.base; radius: 10 }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        // Qt Quick has no clipboard API; a hidden editor copies its selection.
        // Inside the layout so the Pane still sizes itself from one child.
        TextEdit { id: clipboardHelper; visible: false; textFormat: TextEdit.PlainText }
        Label {
            objectName: "endpointTreeTitle"
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: "DEVICE ENDPOINTS"
            elide: Text.ElideRight
            color: panel.palette.text
            font.pixelSize: 11
            font.letterSpacing: 1
        }
        RowLayout {
            Layout.fillWidth: true
            visible: panel.bulkWatchAvailable && panel.watchMarks.length > 0
            Button {
                objectName: "endpointAddSelectedWatches"
                text: "Add " + panel.watchMarks.length
                enabled: panel.watchMarks.length > 0 && panel.provider.watchActive !== true
                onClicked: panel.addMarkedWatches()
                Accessible.name: "Add selected fields to Watchlist"
                ToolTip.visible: hovered
                ToolTip.text: "Add selected scalar and enum fields to Watchlist; unsupported fields are skipped."
            }
            ToolButton {
                objectName: "endpointClearWatchSelection"
                text: "Clear"
                onClicked: panel.clearWatchMarks()
                Accessible.name: "Clear selected Watchlist fields"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: searchInput
                objectName: "endpointSearch"
                Layout.fillWidth: true
                placeholderText: "Search endpoints"
                maximumLength: 256
                onTextChanged: panel.scheduleSearch()
                Keys.onReturnPressed: function(event) { panel.searchEnter(event) }
                Keys.onEnterPressed: function(event) { panel.searchEnter(event) }
                // Down moves into the results (or the tree) with the keyboard.
                Keys.onDownPressed: function(event) {
                    if (searchList.visible && searchList.count > 0) {
                        searchList.currentIndex = 0
                        searchList.positionViewAtBeginning()
                        if (searchList.currentItem)
                            searchList.currentItem.forceActiveFocus()
                    } else if (endpointTree.visible) {
                        endpointTree.forceActiveFocus()
                    }
                    event.accepted = true
                }
            }
            ToolButton {
                text: "Clear"
                enabled: searchInput.text.length > 0
                onClicked: { searchInput.clear(); searchInput.forceActiveFocus() }
                Accessible.name: "Clear endpoint search"
            }
        }
        Label {
            objectName: "endpointSearchStatus"
            visible: searchInput.text.trim().length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: panel.palette.text
            text: panel.searchRunning ? "Searching…" :
                  panel.searchLimited ? "Search limited; refine search" :
                  panel.searchResults.length === 0 ? "No matching endpoints" :
                  panel.searchResults.length + " matching endpoints" + panel.enterHint()
        }
        Label {
            objectName: "endpointWatchSelectionStatus"
            visible: text.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: panel.palette.text
            text: panel.watchMarkStatus
        }
        TreeView {
            id: endpointTree
            objectName: "endpointTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: searchInput.text.trim().length === 0
            clip: true
            model: panel.provider.endpoints
            // Current keyboard row is a navigation cursor, not operation
            // selection. Only an explicit click/Enter asks the provider.
            activeFocusOnTab: true
            selectionModel: ItemSelectionModel { model: endpointTree.model }
            Keys.onReturnPressed: function(event) {
                const index = endpointTree.selectionModel.currentIndex
                if (index.valid)
                    panel.provider.selectEndpoint(index)
            }
            Keys.onEnterPressed: function(event) {
                const index = endpointTree.selectionModel.currentIndex
                if (index.valid)
                    panel.provider.selectEndpoint(index)
            }
            Keys.onSpacePressed: function(event) {
                const index = endpointTree.selectionModel.currentIndex
                if (panel.canMarkWatch(index)) {
                    panel.toggleWatchMark(index)
                    event.accepted = true
                }
            }
            // The Menu key or Shift+F10 opens the actions of the current row.
            Keys.onPressed: function(event) {
                if (event.key !== Qt.Key_Menu && !(event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)))
                    return
                const index = endpointTree.selectionModel.currentIndex
                const row = index.valid ? endpointTree.rowAtIndex(index) : -1
                if (row < 0)
                    return
                // itemAtCell, not itemAtIndex: the library's floor is Qt 6.4.
                // A row scrolled out of view has no delegate until shown.
                let cell = endpointTree.itemAtCell(Qt.point(0, row))
                if (!cell) {
                    endpointTree.positionViewAtRow(row, TableView.Contain)
                    endpointTree.forceLayout()
                    cell = endpointTree.itemAtCell(Qt.point(0, row))
                }
                if (cell && panel.openEndpointMenu(index, cell.isEndpoint, cell))
                    event.accepted = true
            }
            columnWidthProvider: function() { return Math.max(1, endpointTree.width) }
            onWidthChanged: Qt.callLater(function() { if (endpointTree) endpointTree.forceLayout() })
            delegate: TreeViewDelegate {
                id: endpointDelegate
                objectName: "endpointRow"
                required property string name
                required property string detail
                required property bool isEndpoint
                readonly property var sourceIndex: endpointTree.modelIndex(Qt.point(0, row))
                implicitWidth: endpointTree.width
                implicitHeight: 38
                text: name
                // Optional for third-party providers. Selection stays owned by the
                // provider, including when it refuses a click while busy.
                highlighted: isEndpoint && panel.selectedEndpointIndex !== undefined
                    && endpointTree.modelIndex(Qt.point(0, row)) === panel.selectedEndpointIndex
                Rectangle {
                    objectName: "endpointFocusIndicator"
                    anchors.fill: parent
                    color: "transparent"
                    border.color: endpointDelegate.highlighted ? endpointDelegate.palette.highlightedText : panel.palette.text
                    border.width: 2
                    radius: 3
                    visible: endpointTree.activeFocus && endpointDelegate.current
                }
                PlainToolTip {
                    objectName: "endpointToolTip"
                    visible: endpointDelegate.hovered && endpointDelegate.detail.length > 0
                    text: endpointDelegate.detail
                }
                // Basic TreeViewDelegate reads model.display directly.
                // A retired model may have no role data before its reset.
                contentItem: Label {
                    textFormat: Text.PlainText
                    text: endpointDelegate.name
                    elide: Text.ElideRight
                    leftPadding: treeWatchCheck.visible ? treeWatchCheck.implicitWidth + 4 : 0
                    color: endpointDelegate.highlighted
                        ? endpointDelegate.palette.highlightedText
                        : endpointDelegate.palette.buttonText
                    CheckBox {
                        id: treeWatchCheck
                        objectName: "endpointWatchCheck"
                        focusPolicy: Qt.NoFocus
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        visible: panel.canMarkWatch(endpointDelegate.sourceIndex)
                        checked: panel.isWatchMarked(endpointDelegate.sourceIndex)
                        onClicked: panel.toggleWatchMark(endpointDelegate.sourceIndex)
                        Accessible.name: "Select " + endpointDelegate.name + " for Watchlist"
                    }
                }
                onClicked: {
                    if (isEndpoint)
                        panel.provider.selectEndpoint(endpointTree.modelIndex(Qt.point(0, row)))
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) {
                        panel.openEndpointMenu(endpointDelegate.sourceIndex, endpointDelegate.isEndpoint,
                                               endpointDelegate, point.position.x, point.position.y)
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
            Component.onCompleted: Qt.callLater(function() {
                if (!endpointTree) return
                endpointTree.forceLayout()
                endpointTree.expandRecursively(0, 2)
            })
        }
        ListView {
            id: searchList
            objectName: "endpointSearchResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: searchInput.text.trim().length > 0
            clip: true
            model: panel.searchResults
            delegate: ItemDelegate {
                id: searchDelegate
                required property var modelData
                required property int index
                objectName: "endpointSearchResult"
                width: searchList.width
                text: modelData.path
                highlighted: panel.selectedEndpointIndex !== undefined &&
                             modelData.index === panel.selectedEndpointIndex
                function activateResult() {
                    if (modelData.generation === panel.searchGeneration && modelData.index.valid)
                        panel.provider.selectEndpoint(modelData.index)
                }
                onClicked: activateResult()
                Keys.onReturnPressed: function(event) { activateResult(); event.accepted = true }
                Keys.onEnterPressed: function(event) { activateResult(); event.accepted = true }
                Keys.onSpacePressed: function(event) {
                    if (modelData.generation === panel.searchGeneration && panel.canMarkWatch(modelData.index)) {
                        panel.toggleWatchMark(modelData.index)
                        event.accepted = true
                    }
                }
                Keys.onUpPressed: function(event) {
                    if (searchDelegate.index === 0) {
                        searchInput.forceActiveFocus()
                    } else {
                        searchList.currentIndex = searchDelegate.index - 1
                        searchList.currentItem.forceActiveFocus()
                    }
                    event.accepted = true
                }
                Keys.onDownPressed: function(event) {
                    if (searchDelegate.index + 1 < searchList.count) {
                        searchList.currentIndex = searchDelegate.index + 1
                        searchList.currentItem.forceActiveFocus()
                    }
                    event.accepted = true
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                        if (modelData.generation === panel.searchGeneration)
                            panel.openEndpointMenu(modelData.index, true, searchDelegate)
                        event.accepted = true
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) {
                        if (searchDelegate.modelData.generation === panel.searchGeneration)
                            panel.openEndpointMenu(searchDelegate.modelData.index, true, searchDelegate,
                                                   point.position.x, point.position.y)
                    }
                }
                background: Rectangle {
                    objectName: "endpointSearchResultBackground"
                    radius: 3
                    color: searchDelegate.highlighted ? searchDelegate.palette.highlight : "transparent"
                    border.width: searchDelegate.activeFocus ? 2 : 0
                    border.color: searchDelegate.highlighted ? searchDelegate.palette.highlightedText : searchDelegate.palette.text
                }
                contentItem: Label {
                    textFormat: Text.PlainText
                    text: searchDelegate.text
                    elide: Text.ElideLeft
                    leftPadding: searchWatchCheck.visible ? searchWatchCheck.implicitWidth + 4 : 0
                    color: searchDelegate.highlighted ? searchDelegate.palette.highlightedText : searchDelegate.palette.buttonText
                    CheckBox {
                        id: searchWatchCheck
                        objectName: "endpointSearchWatchCheck"
                        focusPolicy: Qt.NoFocus
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        visible: searchDelegate.modelData.generation === panel.searchGeneration &&
                                 panel.canMarkWatch(searchDelegate.modelData.index)
                        checked: panel.isWatchMarked(searchDelegate.modelData.index)
                        onClicked: panel.toggleWatchMark(searchDelegate.modelData.index)
                        Accessible.name: "Select " + searchDelegate.text + " for Watchlist"
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        Label { objectName: "endpointTreeHelp"; visible: panel.height >= 360; textFormat: Text.PlainText; text: "Commands and Services have an explicit Request editor. Each action submits one request."; color: panel.palette.text; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
    }
    Connections {
        target: panel.provider.endpoints
        ignoreUnknownSignals: true
        function onModelAboutToBeReset() { panel.clearWatchMarks(); panel.cancelSearch() }
        function onModelReset() {
            panel.scheduleSearch()
            Qt.callLater(function() { if (endpointTree) endpointTree.expandRecursively(0, 2) })
        }
        function onRowsAboutToBeRemoved() { panel.clearWatchMarks(); panel.cancelSearch() }
        function onRowsRemoved() { panel.scheduleSearch() }
        function onRowsAboutToBeInserted() { panel.clearWatchMarks(); panel.cancelSearch() }
        function onRowsInserted() { panel.scheduleSearch() }
        function onRowsAboutToBeMoved() { panel.clearWatchMarks(); panel.cancelSearch() }
        function onRowsMoved() { panel.scheduleSearch() }
        function onLayoutAboutToBeChanged() { panel.clearWatchMarks(); panel.cancelSearch() }
        function onLayoutChanged() { panel.scheduleSearch() }
        function onDataChanged() { panel.scheduleSearch() }
        function onDestroyed() { panel.clearWatchMarks(); panel.cancelSearch() }
    }
}
