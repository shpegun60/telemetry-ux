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
    // A host may defer explicit selection for a draft-discard decision. The
    // provider remains the selection owner while that decision is pending.
    property var selectionHandler: null
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
    // A provider with watch boxes (watchState, setWatchMany, undoWatch): a
    // box means watched, at once, on Fields and on groups alike. Without them
    // the boxes mark Fields for "Add N", as before.
    readonly property bool watchApi: provider &&
        typeof provider.watchState === "function" &&
        typeof provider.setWatchMany === "function"
    // Moves whenever a box may show something else; the rows ask again then.
    readonly property int watchRevision: watchApi ? provider.watchRevision : 0
    readonly property bool watchLocked: provider && provider.watchActive === true
    // The revision the status line spoke at: another change clears it.
    property int statusRevision: -1
    property bool statusUndo: false
    // The status line is short; this is the whole sentence, for its tooltip
    // and for a screen reader.
    property string watchStatusFull: ""
    readonly property string pollingShort: qsTr("Stop polling to change the Watchlist.")
    readonly property string pollingFull: qsTr("Stop polling to change the Watchlist.")
    // Off until the user turns it on: a preview reads the device in the
    // background, which nobody asked for by opening the tree.
    property bool previewValues: false
    // Said while the tree has no rows; a host words it for its state
    // ("Reconnect to see…" after a dropped connection).
    property string emptyText: qsTr("Connect to see the device's Fields, Commands and Services.")
    readonly property bool previewsSupported: !!provider && typeof provider.previewsSupported === "boolean" && provider.previewsSupported &&
        typeof provider.setPreviewDemand === "function" && typeof provider.clearPreviewDemand === "function"
    readonly property bool previewDemandActive: previewsSupported && previewValues && visible && width > 0 && height > 0 &&
        searchInput.text.trim().length === 0 && !!panel.Window.window && panel.Window.window.visible &&
        panel.Window.window.visibility !== Window.Minimized
    property var previewDemandProvider: null
    readonly property var columnTitles: [qsTr("Name"), qsTr("Value"), qsTr("Type"), qsTr("rw")]
    // The row family's columns (family_spec A3). Value is there only while
    // previews are shown; it and Type are as wide as the widest text the rows
    // have shown, growing only, so a refresh never moves them. When the names
    // of a second-level row run short, Access gives way first, then Type
    // narrows, then Type goes. Never a horizontal scroll.
    readonly property bool valueColumnShown: previewsSupported && previewValues
    readonly property real columnGap: Math.round(Type.bodyPixels * 0.9)
    property real valueSeen: 0
    property real typeSeen: 0
    // Rows report while the tree lays them out, possibly in the middle of
    // working out a column: their widths are gathered and applied on the
    // next turn of the event loop, never during that work.
    property real valuePending: 0
    property real typePending: 0
    function noteMeasured(value, type) {
        if (value > valuePending) valuePending = value
        if (type > typePending) typePending = type
        applyMeasured.restart()
    }
    Timer {
        id: applyMeasured
        interval: 0
        onTriggered: {
            if (panel.valuePending > panel.valueSeen) panel.valueSeen = Math.ceil(panel.valuePending)
            if (panel.typePending > panel.typeSeen) panel.typeSeen = Math.ceil(panel.typePending)
        }
    }
    // Forgotten widths are collected again from the rows already laid out:
    // a row reports only when its texts change.
    function forgetMeasured() { valueSeen = 0; typeSeen = 0; valuePending = 0; typePending = 0; remeasureLater.restart() }
    onValueColumnShownChanged: { valueSeen = 0; valuePending = 0; remeasureLater.restart() }
    Connections { target: Type; function onBodyPixelsChanged() { panel.forgetMeasured() } }
    Timer {
        id: remeasureLater
        interval: 0
        onTriggered: {
            for (let row = endpointTree.topRow; row >= 0 && row <= endpointTree.bottomRow; ++row) {
                const cell = endpointTree.itemAtCell(Qt.point(0, row))
                if (cell && typeof cell.remeasure === "function")
                    cell.remeasure()
            }
        }
    }
    TextMetrics { id: accessMetrics; font.family: Type.fixedFamily; font.pointSize: Type.caption; text: "rw" }
    readonly property real scrollInset: endpointScroll.visible ? endpointScroll.width : 0
    readonly property real valueColumn: valueColumnShown
        ? Math.max(Math.round(Type.bodyPixels * 4.2), Math.min(valueSeen, Math.round(Type.bodyPixels * 16.5), endpointTree.width * 0.4))
        : 0
    readonly property real typeFull: Math.max(Math.round(Type.bodyPixels * 3), Math.min(typeSeen, Math.round(Type.bodyPixels * 9.6)))
    readonly property real accessFull: Math.ceil(accessMetrics.advanceWidth)
    // Where a second-level row's name starts: padding, two indents, the
    // chevron, the box's slot and its gap.
    readonly property real nameStart: Type.padding + Type.indent * 3 + Type.rowHeight + Type.tight
    function nameRoom(access, type) {
        return endpointTree.width - scrollInset - Type.padding - nameStart
            - (valueColumn > 0 ? valueColumn + columnGap : 0)
            - (type > 0 ? type + columnGap : 0) - (access > 0 ? access + columnGap : 0)
    }
    readonly property real accessColumn: nameRoom(accessFull, typeFull) >= Type.bodyPixels * 10.5 ? accessFull : 0
    readonly property real typeColumn: nameRoom(accessColumn, typeFull) >= Type.bodyPixels * 9 ? typeFull
        : nameRoom(accessColumn, Math.round(Type.bodyPixels * 3)) >= Type.bodyPixels * 7.2 ? Math.round(Type.bodyPixels * 3) : 0
    // The columns' left edges in the tree's coordinates, right to left.
    readonly property real accessLeft: endpointTree.width - scrollInset - Type.padding - accessColumn
    readonly property real typeLeft: accessLeft - (accessColumn > 0 ? columnGap : 0) - typeColumn
    readonly property real valueLeft: typeLeft - (typeColumn > 0 ? columnGap : 0) - valueColumn
    // A category's bare number, a group's endpoints in words.
    function countText(count, category, depth) {
        if (!(count > 0)) return ""
        if (depth === 0) return qsTr("%L1").arg(count)
        if (category === 2) return count === 1 ? qsTr("1 Command") : qsTr("%L1 Commands").arg(count)
        if (category === 3) return count === 1 ? qsTr("1 Service") : qsTr("%L1 Services").arg(count)
        return fieldsText(count)
    }
    // Why a Field, a member or a group of them has its box off: the
    // provider's words where it says, none from older providers.
    function refusalOf(index, revision) {
        return watchApi && index && index.valid && typeof provider.watchRefusal === "function"
            ? provider.watchRefusal(index) : ""
    }

    function clearPreviews() {
        const prior = previewDemandProvider
        previewDemandProvider = null
        if (prior && typeof prior.clearPreviewDemand === "function")
            prior.clearPreviewDemand(panel)
    }
    function updatePreviewDemand() {
        if (!previewDemandActive) {
            clearPreviews()
            return
        }
        const indexes = []
        // Only loaded cells intersecting the viewport, never a model traversal.
        for (let row = endpointTree.topRow; row >= 0 && row <= endpointTree.bottomRow; ++row) {
            const cell = endpointTree.itemAtCell(Qt.point(0, row))
            // A folded struct is read too: its summary is its value.
            if (!cell || !cell.visible || cell.y + cell.height <= endpointTree.contentY ||
                cell.y >= endpointTree.contentY + endpointTree.height || !cell.previewEligible)
                continue
            const index = cell.sourceIndex
            if (index && index.valid)
                indexes.push(index)
        }
        const currentProvider = provider
        if (previewDemandProvider && previewDemandProvider !== currentProvider)
            clearPreviews()
        previewDemandProvider = currentProvider
        currentProvider.setPreviewDemand(panel, indexes)
    }
    function suspendPreviews() {
        clearPreviews()
        if (previewDemandActive)
            previewDemandTimer.restart()
    }
    onPreviewDemandActiveChanged: suspendPreviews()
    Component.onDestruction: clearPreviews()

    // Every explicit activation passes its source index through this route.
    // Never optimistically change selectedEndpointIndex or the row highlight.
    function requestSelection(index) {
        if (!panel.provider || !index || !index.valid)
            return false
        if (typeof panel.selectionHandler === "function")
            return panel.selectionHandler(index)
        return panel.provider.selectEndpoint(index)
    }

    // A row's box: -1 none, else Qt.Unchecked, PartiallyChecked or Checked.
    // revision is only there so a binding asks again when it moves.
    function stateOf(index, revision) {
        return watchApi && index && index.valid ? provider.watchState(index) : -1
    }
    function fieldsText(count) {
        return count === 1 ? qsTr("1 Field") : qsTr("%L1 Fields").arg(count)
    }
    // Ticks or unticks a row: a Field, or every Field under a group. All of
    // a group or none: a group that does not fit changes nothing.
    function toggleWatch(index) {
        if (!watchApi || !index || !index.valid)
            return false
        if (watchLocked) {
            say(pollingShort, pollingFull, false)
            return false
        }
        const state = stateOf(index, watchRevision)
        if (state < 0)
            return false
        const group = isGroup(index)
        // A group by its own name ("signals"), a Field by its path.
        return applyWatch([index], state !== Qt.Checked, group ? nameOf(index) : pathOf(index), group)
    }
    // Watches or stops watching the Fields under indexes, as one change, and
    // says what it did. A single Field says nothing when it worked.
    function applyWatch(indexes, watched, label, group) {
        if (watchLocked) {
            say(pollingShort, pollingFull, false)
            return false
        }
        const result = provider.setWatchMany(indexes, watched)
        if (!result || result.status === "capacity") {
            const fields = result ? result.fields : 0, room = result ? result.room : 0
            say(qsTr("No room: %L1 Fields, %L2 free").arg(fields).arg(room),
                qsTr("%1 has %L2 Fields; the Watchlist has room for %L3 more of its %L4.")
                    .arg(label).arg(fields).arg(room).arg(provider.watchLimit), false)
            return false
        }
        if (result.status !== "ok") {
            say(qsTr("Not changed: %1").arg(result.status),
                qsTr("Could not change the Watchlist: %1.").arg(result.status), false)
            return false
        }
        if (!group)
            say("", "", false)
        else if (result.changed === 0)
            say(watched ? qsTr("Already watched") : qsTr("Nothing watched"),
                watched ? qsTr("Everything in %1 is already watched.").arg(label)
                        : qsTr("Nothing in %1 is watched.").arg(label), false)
        else if (watched)
            say(qsTr("Added %L1").arg(result.changed),
                qsTr("Added %1 from %2 to the Watchlist.").arg(fieldsText(result.changed)).arg(label), true)
        else
            say(qsTr("Removed %L1").arg(result.changed),
                qsTr("Removed %1 of %2 from the Watchlist.").arg(fieldsText(result.changed)).arg(label), true)
        return result.changed > 0
    }
    function undoWatch() {
        if (!watchApi || typeof provider.undoWatch !== "function")
            return false
        const result = provider.undoWatch()
        const done = result && result.status === "ok"
        say(done ? qsTr("Undone") : qsTr("Nothing to undo"),
            done ? qsTr("The last change to the Watchlist is undone.") : qsTr("There is nothing to undo."), false)
        return done
    }
    // Says what the last action did, at the revision it left: a short line,
    // and the whole sentence for its tooltip.
    function say(text, full, undoable) {
        watchMarkStatus = text
        watchStatusFull = full
        statusUndo = undoable
        statusRevision = watchRevision
    }
    onWatchRevisionChanged: {
        if (statusRevision >= 0 && watchRevision !== statusRevision) {
            watchMarkStatus = ""
            watchStatusFull = ""
            statusUndo = false
            statusRevision = -1
        }
    }

    function clearWatchMarks() {
        ++watchMarkGeneration
        watchMarks = []
        watchMarkStatus = ""
        watchStatusFull = ""
        statusUndo = false
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
        if (!group || !group.valid)
            return { indexes: [], limited: false }
        return candidatesUnder(group)
    }
    // The same walk from any parent; an invalid index is the whole tree.
    function candidatesUnder(group) {
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
        if (searchSource)
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
        if (watchApi)
            return stateOf(index, watchRevision) >= 0 ? applyWatch([index], true, pathOf(index), false) : false
        return canMarkWatch(index) ? watchIndexes([index], pathOf(index), false, false) : false
    }
    function watchGroup(index) {
        if (watchApi)
            return applyWatch([index], true, nameOf(index), true)
        const walk = groupCandidates(index)
        return watchIndexes(walk.indexes, pathOf(index), true, walk.limited)
    }
    // Every Field of the device the Watchlist can hold, in tree order, up to
    // its row limit (the rest is counted as capacity). For a host's menu too.
    function watchAll() {
        if (!searchSource)
            return false
        if (watchApi) {
            const roots = []
            for (let row = 0; row < searchSource.rowCount(); ++row)
                roots.push(searchSource.index(row, 0))
            return applyWatch(roots, true, qsTr("the device"), true)
        }
        const walk = candidatesUnder(searchSource.index(-1, -1))
        return watchIndexes(walk.indexes, "the device", true, walk.limited)
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
        if (!searchSource || !index || !index.valid || searchSource.rowCount(index) <= 0)
            return false
        // This model query is available immediately during discovery/reset;
        // DelegateModel's presentation items may not have been incubated yet.
        if (typeof searchSource.isGroup === "function")
            return searchSource.isGroup(index)
        nameModel.rootIndex = index.parent
        const item = index.row >= 0 && index.row < nameModel.count ? nameModel.items.get(index.row) : null
        return !!item && !!item.model && item.model.isEndpoint !== true && item.model.previewChild !== true
    }
    function openInitialGroups() {
        if (!openOnLoad)
            return
        // A connected model can reset after this panel was created empty.
        // Materialize its new root rows before inspecting the flattened view.
        endpointTree.forceLayout()
        if (endpointTree.rows === 0)
            return
        const first = endpointTree.modelIndex(Qt.point(0, 0))
        if (!isGroup(first))
            return
        // Exactly the category and catalog levels. Field value children start
        // below them and remain collapsed; do not walk a changing flat row list.
        endpointTree.expandRecursively(0, 2)
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
        if (watchApi ? stateOf(first.index, watchRevision) >= 0 : canMarkWatch(first.index))
            watchIndex(first.index)
        else
            panel.requestSelection(first.index)
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
        if (watchApi ? stateOf(first.index, watchRevision) < 0 : !canMarkWatch(first.index))
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
    onProviderChanged: { clearPreviews(); clearWatchMarks() }
    onSearchSourceChanged: { clearPreviews(); clearWatchMarks(); scheduleSearch() }
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
    // A host waits for the menu's focus restoration before asking for input.
    readonly property bool contextMenuVisible: endpointMenu.visible
    // Borrowed pre-menu origin; a host can preserve it before Popup.Window
    // changes native focus. It becomes null when the menu finishes closing.
    readonly property Item contextMenuReturnFocus: endpointMenu.returnFocus
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
            readonly property int boxState: panel.watchApi && endpointMenu.targetIsEndpoint
                                            ? panel.stateOf(endpointMenu.target, panel.watchRevision) : -1
            text: boxState === Qt.Checked ? qsTr("Stop watching") : qsTr("Watch")
            enabled: panel.provider.watchActive !== true &&
                     (panel.watchApi ? boxState >= 0 : panel.canMarkWatch(endpointMenu.target))
            onTriggered: {
                const target = panel.menuTarget()
                if (target && panel.watchApi)
                    panel.toggleWatch(target)
                else if (target)
                    panel.watchIndex(target)
            }
        }
        MenuItem {
            objectName: "endpointMenuWatchGroup"
            readonly property int boxState: panel.watchApi && endpointMenu.targetIsGroup
                                            ? panel.stateOf(endpointMenu.target, panel.watchRevision) : -1
            text: !panel.watchApi ? qsTr("Watch this group")
                : boxState === Qt.Checked ? qsTr("Stop watching group") : qsTr("Watch all in group")
            enabled: panel.bulkWatchAvailable && endpointMenu.targetIsGroup && panel.provider.watchActive !== true &&
                     (!panel.watchApi || boxState >= 0)
            onTriggered: {
                const target = panel.menuTarget()
                if (target && panel.watchApi)
                    panel.toggleWatch(target)
                else if (target)
                    panel.watchGroup(target)
            }
        }
        MenuItem {
            objectName: "endpointMenuWatchAll"
            text: "Watch all Fields"
            enabled: panel.bulkWatchAvailable && panel.provider.watchActive !== true
            onTriggered: panel.watchAll()
        }
        MenuItem {
            objectName: "endpointMenuDetails"
            text: "Show details"
            enabled: endpointMenu.targetIsEndpoint
            onTriggered: { const target = panel.menuTarget(); if (target) panel.requestSelection(target) }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "endpointMenuCopyPath"
            text: "Copy path"
            onTriggered: { const target = panel.menuTarget(); if (target) panel.copyPath(target) }
        }
    }
    Timer { id: searchTimer; repeat: false; onTriggered: panel.scanSearch() }
    // Demand publication is cheap metadata work; the provider schedules I/O.
    Timer {
        id: previewDemandTimer
        objectName: "endpointPreviewDemandTimer"
        interval: 250
        repeat: true
        running: panel.previewDemandActive
        onTriggered: panel.updatePreviewDemand()
    }
    // The defaults are the Theme's roles; a host may still set the palette.
    palette.window: Theme.surfaceWindow
    palette.windowText: Theme.textPrimary
    // The docks' surface, as descriptor.bin's; a host may set its own.
    palette.base: Theme.surfaceWindow
    palette.alternateBase: Theme.surfaceAlternate
    palette.text: Theme.textPrimary
    palette.button: Theme.buttonFill
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.stateSelected
    palette.highlightedText: Theme.selectedForeground
    palette.placeholderText: Theme.textMuted
    // A card on its own; inside a dock, which titles and frames it, a plain
    // surface edge to edge.
    property bool framed: true
    // Whether a new tree opens its Fields as it arrives; a program that leaves
    // every branch for the user to open sets it false.
    property bool openOnLoad: true
    padding: framed ? 12 : Type.padding
    background: Rectangle { objectName: "endpointTreePanelBackground"; color: panel.palette.base; radius: panel.framed ? 10 : 0 }
    ColumnLayout {
        anchors.fill: parent
        spacing: Type.spacing
        // Qt Quick has no clipboard API; a hidden editor copies its selection.
        // Inside the layout so the Pane still sizes itself from one child.
        TextEdit { id: clipboardHelper; visible: false; textFormat: TextEdit.PlainText }
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
        // Nothing to find before the tree has rows: the empty text says why.
        RowLayout {
            Layout.fillWidth: true
            visible: endpointTree.rows > 0 || searchInput.text.length > 0
            spacing: Type.spacing
            TextField {
                id: searchInput
                objectName: "endpointSearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Find by name or path (Ctrl+F)")
                maximumLength: 256
                // Room for the clear button inside the field.
                rightPadding: searchClear.visible ? searchClear.width + 4 : leftPadding
                onTextChanged: panel.scheduleSearch()
                Keys.onEscapePressed: function(event) {
                    if (text.length === 0)
                        return
                    clear()
                    event.accepted = true
                }
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
                ToolButton {
                    id: searchClear
                    objectName: "endpointSearchClear"
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: height
                    height: parent.height - 6
                    visible: searchInput.text.length > 0
                    focusPolicy: Qt.NoFocus
                    text: "✕"
                    onClicked: { searchInput.clear(); searchInput.forceActiveFocus() }
                    Accessible.name: qsTr("Clear the search")
                }
            }
            CheckBox {
                id: previewToggle
                objectName: "endpointPreviewValues"
                visible: panel.previewsSupported
                text: qsTr("Preview values")
                checked: panel.previewValues
                onToggled: panel.previewValues = checked
                Accessible.name: qsTr("Preview values")
                Accessible.description: qsTr("Read visible Fields slowly while this tree is shown. This does not change the Watchlist.")
                ToolTip.visible: hovered
                ToolTip.text: previewToggle.Accessible.description
            }
        }
        Label {
            objectName: "endpointSearchStatus"
            visible: searchInput.text.trim().length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pointSize: Type.caption
            color: panel.palette.text
            text: panel.searchRunning ? "Searching…" :
                  panel.searchLimited ? "Search limited; refine search" :
                  panel.searchResults.length === 0 ? "No matching endpoints" :
                  panel.searchResults.length + " matching endpoints" + panel.enterHint()
        }
        Item {
            objectName: "endpointColumnHeaders"
            visible: searchInput.text.trim().length === 0 && endpointTree.rows > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Type.rowHeight
            component ColumnTitle: Label {
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: Theme.textSecondary
                Accessible.role: Accessible.ColumnHeader
                Accessible.name: text
            }
            ColumnTitle {
                objectName: "endpointColumnHeader_0"
                x: Type.padding
                width: Math.max(0, (panel.valueColumn > 0 ? panel.valueLeft : panel.typeColumn > 0 ? panel.typeLeft
                                    : panel.accessLeft) - panel.columnGap - x)
                text: panel.columnTitles[0]
            }
            ColumnTitle {
                objectName: "endpointColumnHeader_1"
                visible: panel.valueColumn > 0
                x: panel.valueLeft
                width: panel.valueColumn
                horizontalAlignment: Text.AlignRight
                text: panel.columnTitles[1]
            }
            ColumnTitle {
                objectName: "endpointColumnHeader_2"
                visible: panel.typeColumn > 0
                x: panel.typeLeft
                width: panel.typeColumn
                text: panel.columnTitles[2]
            }
            ColumnTitle {
                objectName: "endpointColumnHeader_3"
                visible: panel.accessColumn > 0
                x: panel.accessLeft
                width: panel.accessColumn
                font.family: Type.fixedFamily
                text: panel.columnTitles[3]
                Accessible.name: qsTr("Access")
                HoverHandler { id: accessTitleHover }
                PlainToolTip { visible: accessTitleHover.hovered; text: qsTr("Access: r can be read, w can be written"); delay: 700 }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.outlineVariant
                Accessible.ignored: true
            }
        }
        TreeView {
            id: endpointTree
            // A zoom changes the row height: the rows already laid out follow it.
            Connections {
                target: Type
                function onRowHeightChanged() { Qt.callLater(function() { if (endpointTree) endpointTree.forceLayout() }) }
            }
            objectName: "endpointTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: searchInput.text.trim().length === 0
            clip: true
            model: panel.provider.endpoints
            onTopRowChanged: panel.suspendPreviews()
            onBottomRowChanged: panel.suspendPreviews()
            onExpanded: panel.suspendPreviews()
            onCollapsed: panel.suspendPreviews()
            // Current keyboard row is a navigation cursor, not operation
            // selection. Only an explicit click/Enter asks the provider.
            activeFocusOnTab: true
            selectionModel: ItemSelectionModel { model: endpointTree.model }
            Keys.onReturnPressed: function(event) {
                const index = endpointTree.selectionModel.currentIndex
                if (index.valid)
                    panel.requestSelection(index)
            }
            Keys.onEnterPressed: function(event) {
                const index = endpointTree.selectionModel.currentIndex
                if (index.valid)
                    panel.requestSelection(index)
            }
            Keys.onSpacePressed: function(event) {
                const index = endpointTree.selectionModel.currentIndex
                if (panel.watchApi) {
                    if (panel.stateOf(index, panel.watchRevision) >= 0) {
                        panel.toggleWatch(index)
                        event.accepted = true
                    }
                } else if (panel.canMarkWatch(index)) {
                    panel.toggleWatchMark(index)
                    event.accepted = true
                }
            }
            // The Menu key or Shift+F10 opens the actions of the current row.
            Keys.onPressed: function(event) {
                // Ctrl+F finds, as in the file views; a host's Ctrl+K too.
                if (event.key === Qt.Key_F && (event.modifiers & Qt.ControlModifier)) {
                    panel.find()
                    event.accepted = true
                    return
                }
                if (event.key !== Qt.Key_Menu && !(event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)))
                    return
                const index = endpointTree.selectionModel.currentIndex
                const row = index.valid ? endpointTree.rowAtIndex(index) : -1
                if (row < 0)
                    return
                // A row scrolled out of view has no delegate until shown.
                let cell = endpointTree.itemAtIndex(index)
                if (!cell) {
                    endpointTree.positionViewAtIndex(index, TableView.Contain)
                    endpointTree.forceLayout()
                    cell = endpointTree.itemAtIndex(index)
                }
                if (cell && panel.openEndpointMenu(index, cell.isEndpoint, cell))
                    event.accepted = true
            }
            // Striped by the rows themselves (family_spec A9): the eye
            // crosses a wide gap from a name to its value.
            alternatingRows: false
            columnWidthProvider: function() { return endpointTree.width }
            rowHeightProvider: function() { return Type.rowHeight }
            onWidthChanged: Qt.callLater(function() { if (endpointTree) endpointTree.forceLayout() })
            // A retired model may have no role data before its reset; the row
            // reads its required roles, never model.display.
            delegate: EndpointTreeRow {
                id: endpointDelegate
                tableMode: true
                nameColumnWidth: endpointTree.width
                valueColumnWidth: panel.valueColumn
                typeColumnWidth: panel.typeColumn
                accessColumnWidth: panel.accessColumn
                columnGap: panel.columnGap
                rightClearance: panel.scrollInset
                countText: typeof model.count === "number" ? panel.countText(model.count, model.category, depth) : ""
                watchRefusal: panel.refusalOf(sourceIndex, panel.watchRevision)
                onMeasured: function(value, type) { panel.noteMeasured(value, type) }
                typeText: typeof model.typeText === "string" ? model.typeText : ""
                accessText: typeof model.accessText === "string" ? model.accessText : ""
                accessMark: isEndpoint && !previewChild && model.category === 1 &&
                    typeof model.readable === "boolean" && typeof model.writable === "boolean"
                    ? (model.readable ? "r" : "-") + (model.writable ? "w" : "-") : ""
                unit: typeof model.unit === "string" ? model.unit : ""
                previewEligible: model.previewEligible === true
                previewAggregate: model.previewAggregate === true
                previewChild: model.previewChild === true
                previewValue: typeof model.previewValue === "string" ? model.previewValue : ""
                previewState: typeof model.previewState === "string" ? model.previewState : ""
                previewAgeMs: typeof model.previewAgeMs === "number" ? model.previewAgeMs : -1
                previewReason: typeof model.previewReason === "string" ? model.previewReason : ""
                previewsShown: panel.previewsSupported && panel.previewValues
                // Optional for third-party providers. Selection stays owned by the
                // provider, including when it refuses a click while busy.
                highlighted: isEndpoint && panel.selectedEndpointIndex !== undefined
                    && sourceIndex === panel.selectedEndpointIndex
                readonly property int boxState: panel.stateOf(sourceIndex, panel.watchRevision)
                // A row the Watchlist cannot hold yet keeps a box, off, with its reason.
                watchable: panel.watchApi ? boxState >= 0 || watchRefusal.length > 0 : panel.canMarkWatch(sourceIndex)
                watchState: panel.watchApi ? Math.max(0, boxState)
                          : panel.isWatchMarked(sourceIndex) ? Qt.Checked : Qt.Unchecked
                watchEnabled: !panel.watchApi || !panel.watchLocked
                watchOffReason: panel.pollingFull
                hint: detail
                watchName: !panel.watchApi ? "Select " + name + " for Watchlist"
                         : hasChildren ? qsTr("Watch every Field in %1").arg(name) : qsTr("Watch %1").arg(name)
                onWatchToggled: {
                    if (panel.watchApi)
                        panel.toggleWatch(sourceIndex)
                    else
                        panel.toggleWatchMark(sourceIndex)
                }
                onClicked: {
                    if (isEndpoint)
                        panel.requestSelection(sourceIndex)
                }
                onContextMenuRequested: function(x, y) {
                    panel.openEndpointMenu(sourceIndex, isEndpoint, endpointDelegate, x, y)
                }
            }
            ScrollBar.vertical: ScrollBar { id: endpointScroll }
            // Not connected, the tree says how it fills, on the view itself.
            Label {
                objectName: "endpointTreeEmpty"
                parent: endpointTree
                anchors.centerIn: parent
                width: parent.width - 2 * Type.padding
                visible: endpointTree.rows === 0 && panel.provider && panel.provider.connected === false
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: Theme.textMuted
                text: panel.emptyText
            }
            Component.onCompleted: Qt.callLater(function() {
                if (!endpointTree) return
                endpointTree.forceLayout()
                panel.openInitialGroups()
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
                        panel.requestSelection(modelData.index)
                }
                onClicked: activateResult()
                Keys.onReturnPressed: function(event) { activateResult(); event.accepted = true }
                Keys.onEnterPressed: function(event) { activateResult(); event.accepted = true }
                Keys.onSpacePressed: function(event) {
                    if (modelData.generation !== panel.searchGeneration)
                        return
                    if (panel.watchApi) {
                        if (panel.stateOf(modelData.index, panel.watchRevision) >= 0) {
                            panel.toggleWatch(modelData.index)
                            event.accepted = true
                        }
                    } else if (panel.canMarkWatch(modelData.index)) {
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
                        readonly property int boxState: panel.stateOf(searchDelegate.modelData.index,
                                                                      panel.watchRevision)
                        visible: searchDelegate.modelData.generation === panel.searchGeneration &&
                                 (panel.watchApi ? boxState >= 0
                                                 : panel.canMarkWatch(searchDelegate.modelData.index))
                        enabled: !panel.watchApi || !panel.watchLocked
                        checked: panel.watchApi ? boxState === Qt.Checked
                                                : panel.isWatchMarked(searchDelegate.modelData.index)
                        // The provider decides; the box shows what it decided.
                        nextCheckState: function() { return checkState }
                        onClicked: {
                            if (panel.watchApi)
                                panel.toggleWatch(searchDelegate.modelData.index)
                            else
                                panel.toggleWatchMark(searchDelegate.modelData.index)
                        }
                        Accessible.name: "Select " + searchDelegate.text + " for Watchlist"
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        // What the last watch action did, on one line kept at the bottom: always
        // reserved, so nothing moves when it speaks; blank when idle. The whole
        // of a cut line on hover.
        // A fixed height: the tree above takes every other pixel.
        // Undo follows the words it undoes: "Added 4 · Undo".
        RowLayout {
            id: statusRow
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: Math.max(statusMetrics.height + 4, 24)
            Layout.maximumHeight: Layout.preferredHeight
            spacing: Type.spacing
            Label {
                id: watchStatus
                objectName: "endpointWatchSelectionStatus"
                // As wide as its words, and cut only when the line is too narrow.
                Layout.maximumWidth: Math.max(0, statusRow.width - (undoLink.visible
                    ? undoLink.implicitWidth + statusDot.implicitWidth + 2 * statusRow.spacing : 0))
                Layout.alignment: Qt.AlignVCenter
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: panel.palette.text
                // While the Watchlist polls, why the boxes are off, over the
                // last message; that comes back when polling stops.
                readonly property bool polling: panel.watchApi && panel.watchLocked
                readonly property string full: polling ? panel.pollingFull
                                             : panel.watchStatusFull.length > 0 ? panel.watchStatusFull
                                             : panel.watchMarkStatus
                text: polling ? panel.pollingShort : panel.watchMarkStatus
                Accessible.description: full
                HoverHandler { id: watchStatusHover }
                PlainToolTip {
                    visible: watchStatusHover.hovered && watchStatus.full.length > 0 &&
                             (watchStatus.truncated || watchStatus.full !== watchStatus.text)
                    text: watchStatus.full
                }
                FontMetrics { id: statusMetrics; font: watchStatus.font }
            }
            Label {
                id: statusDot
                visible: undoLink.visible
                text: "·"
                color: Theme.textMuted
                font.pointSize: Type.caption
                Accessible.ignored: true
            }
            LinkButton {
                id: undoLink
                objectName: "endpointWatchUndo"
                visible: panel.statusUndo && panel.watchApi && panel.provider.canUndoWatch === true
                text: qsTr("Undo")
                font.pointSize: Type.caption
                onClicked: panel.undoWatch()
            }
            Item { Layout.fillWidth: true }
        }
    }
    // The groups opened after a reset, on the next turn of the event loop. A
    // timer of the panel's own dies with it; a callLater closure outlived an
    // unloaded panel and ran in its destroyed context.
    Timer {
        id: initialGroups
        interval: 0
        onTriggered: panel.openInitialGroups()
    }
    Connections {
        target: panel.provider.endpoints
        ignoreUnknownSignals: true
        function onModelAboutToBeReset() { panel.clearPreviews(); panel.clearWatchMarks(); panel.cancelSearch() }
        function onModelReset() {
            panel.forgetMeasured()
            panel.scheduleSearch()
            initialGroups.restart()
        }
        function onRowsAboutToBeRemoved() { panel.clearPreviews(); panel.clearWatchMarks(); panel.cancelSearch() }
        function onRowsRemoved() { panel.scheduleSearch() }
        function onRowsAboutToBeInserted() { panel.clearPreviews(); panel.clearWatchMarks(); panel.cancelSearch() }
        function onRowsInserted() { panel.scheduleSearch() }
        function onRowsAboutToBeMoved() { panel.clearPreviews(); panel.clearWatchMarks(); panel.cancelSearch() }
        function onRowsMoved() { panel.scheduleSearch() }
        function onLayoutAboutToBeChanged() { panel.clearPreviews(); panel.clearWatchMarks(); panel.cancelSearch() }
        function onLayoutChanged() { panel.scheduleSearch() }
        function onDataChanged(topLeft, bottomRight, roles) {
            if (panel.searchSource && typeof panel.searchSource.previewOnlyChange === "function" &&
                panel.searchSource.previewOnlyChange(roles))
                return
            panel.scheduleSearch()
        }
        function onDestroyed() { panel.clearPreviews(); panel.clearWatchMarks(); panel.cancelSearch() }
    }
}
