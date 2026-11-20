// Reusable presentation: the caller owns the provider and all I/O. MIT.
//
// The Device tree (the designer's, 18 November 2026, and the review of 20
// November): one tree of groups, a group's subgroups first, then its
// Fields, Commands and Services, with no level for the families - a kinds
// filter (the funnel) narrows the tree to them instead. Every row says what
// it is by a glyph and a weight, every Field its type in one column that
// never hides, every group its count in words; one indent step for the
// whole tree, indent guides, the current branch's brighter; a context line
// over the tree names the group the rows in view belong to. Find filters the
// tree in place by name, path or type and steps through the matches; the
// keys fold and step as in Details; what is open survives a reconnect.
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
    // What a row's drag carries (LP-1g3): an endpoint's path, or every
    // endpoint's under a group or a category, in the tree's order.
    function dragPaths(index) {
        const model = provider.endpoints
        if (!model || typeof model.pathOf !== "function" || !index || !index.valid)
            return []
        const out = []
        const walk = (at, depth) => {
            const path = model.pathOf(at)
            // An endpoint, or a value inside one, goes whole; a group or a
            // category gives its endpoints.
            if (path.length > 0 && !model.isGroup(at)) {
                out.push(path)
                return
            }
            if (depth > 6)
                return
            for (let r = 0; r < model.rowCount(at); ++r)
                walk(model.index(r, 0, at), depth + 1)
        }
        walk(index, 0)
        return out
    }
    readonly property var selectedEndpointIndex: provider.selectedEndpointIndex
    // The device's model. The tree shows it through a filter (find), so a
    // row's index is the filter's; everything the provider is asked takes
    // the device's index (sourceOf, shownOf).
    readonly property var searchSource: provider.endpoints
    readonly property int searchDepthLimit: 64
    // These are source-model indexes, independent of the endpoint editor.
    // Never retain them across structural changes to the source model.
    property var watchMarks: []
    property int watchMarkGeneration: 0
    // What the status line says: its UndoNotice's message.
    readonly property alias watchMarkStatus: watchNotice.message
    readonly property int groupWalkLimit: 4096
    // Whether the tree offers watching at all: its boxes, the Add bar and the
    // menu's Watch. False for a second device's tree beside the first (a
    // Compare dock), whose Watchlist is not the one on screen.
    property bool watchBoxes: true
    readonly property bool bulkWatchAvailable: watchBoxes && provider &&
        typeof provider.addWatchMany === "function" &&
        typeof provider.watchCandidate === "function"
    // A provider with watch boxes (watchState, setWatchMany): a box means
    // watched, at once, on Fields and on groups alike. Without them the boxes
    // mark Fields for "Add N", as before.
    readonly property bool watchApi: watchBoxes && provider &&
        typeof provider.watchState === "function" &&
        typeof provider.setWatchMany === "function"
    // Moves whenever a box may show something else; the rows ask again then.
    readonly property int watchRevision: watchApi ? provider.watchRevision : 0
    // Locked while polling only for a provider that cannot take changes then;
    // the Dynamic one takes them between two reads.
    readonly property bool watchLocked: provider && provider.watchActive === true &&
                                        provider.watchChangesWhilePolling !== true
    // The provider's history (UR-1): the status line offers the Undo of the
    // step it speaks of while that step is the one Undo would take.
    readonly property var history: provider && provider.history ? provider.history : null
    readonly property string pollingShort: qsTr("Stop polling to change the Watchlist.")
    readonly property string pollingFull: qsTr("Stop polling to change the Watchlist.")
    // Off until the user turns it on: a preview reads the device in the
    // background, which nobody asked for by opening the tree.
    property bool previewValues: false
    // How often a cycle reads the Fields in view while previews are on (PV-2);
    // turning them on from the menu picks 2 s. A host saves both with its project.
    property int previewIntervalMs: 2000
    // One of the intervals the Values menu offers.
    component PreviewEvery: MenuItem {
        required property int ms
        objectName: "endpointPreviewEvery" + ms
        text: qsTr("Every %1").arg(panel.previewIntervalText(ms))
        checkable: true
        checked: panel.previewValues && panel.previewIntervalMs === ms
        onTriggered: {
            panel.previewIntervalMs = ms
            panel.previewValues = true
        }
    }
    onPreviewIntervalMsChanged: if (previewValues && provider && typeof provider.previewIntervalMs === "number")
                                    provider.previewIntervalMs = previewIntervalMs
    onPreviewValuesChanged: if (previewValues && provider && typeof provider.previewIntervalMs === "number")
                                provider.previewIntervalMs = previewIntervalMs
    // Said while the tree has no rows; a host words it for its state
    // ("Reconnect to see…" after a dropped connection).
    property string emptyText: qsTr("Connect to see the device's Fields, Commands and Services.")
    function previewIntervalText(ms) {
        return ms % 1000 === 0 ? qsTr("%L1 s").arg(ms / 1000) : qsTr("%L1 ms").arg(ms)
    }
    readonly property bool previewsSupported: !!provider && typeof provider.previewsSupported === "boolean" && provider.previewsSupported &&
        typeof provider.setPreviewDemand === "function" && typeof provider.clearPreviewDemand === "function"
    readonly property bool previewDemandActive: previewsSupported && previewValues && visible && width > 0 && height > 0 &&
        !!panel.Window.window && panel.Window.window.visible &&
        panel.Window.window.visibility !== Window.Minimized
    property var previewDemandProvider: null
    readonly property var columnTitles: [qsTr("Name"), qsTr("Value"), qsTr("Type"), qsTr("rw")]

    // The row family's columns (family_spec A3, the Device tree design D1).
    // Type is as wide as the device's widest Field type, measured once per
    // descriptor, and never hides: a narrow tree gives way in this order -
    // rw, then Value (while previews are on) down to its least, then the
    // names down to their first characters, only then a type, row by row
    // (EndpointTreeRow). Never a horizontal scroll.
    readonly property bool valueColumnShown: previewsSupported && previewValues
    readonly property real columnGap: Math.round(Type.bodyPixels * 0.9)
    property real valueSeen: 0
    // A model with no widestFieldType: the widest type a Field's row has shown.
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
    TextMetrics { id: leastTypeMetrics; font.family: Type.fixedFamily; font.pointSize: Type.caption; text: "f32" }
    // The device's widest Field type, as its model knows it.
    readonly property string widestFieldType: searchSource && typeof searchSource.widestFieldType === "string"
                                              ? searchSource.widestFieldType : ""
    TextMetrics { id: widestMetrics; font.family: Type.fixedFamily; font.pointSize: Type.caption; text: panel.widestFieldType }
    readonly property real scrollInset: endpointScroll.visible ? endpointScroll.width : 0
    // W: the widest Field type, 2 to 9.6 body heights.
    readonly property real typeFull: Math.max(Math.round(Type.bodyPixels * 2),
        Math.min(widestFieldType.length > 0 ? Math.ceil(widestMetrics.advanceWidth) : typeSeen, Math.round(Type.bodyPixels * 9.6)))
    readonly property real accessFull: Math.ceil(accessMetrics.advanceWidth)
    // A row's name x in the tree at a depth and a step: the indents, the
    // chevron, the box's slot, the glyph and its gap (90 px at depth 2, 16 px).
    function nameXAt(depth, step) {
        return depth * step + Type.indent + Type.rowHeight + Type.iconSize + Type.spacing
    }
    // What a row has from its name to its right edge, the scroll bar's (the
    // panel's padding is the margin on either side), at the full step.
    function roomAt(depth) {
        return endpointTree.width - scrollInset - nameXAt(depth, Type.indent)
    }
    // The give-way (the designer's review, 14.1): 1 rw and 3 the type
    // scopes are measured on a depth-3 row, 2 Value on a depth-2 row as
    // the kinds design had it, 4 the indent step at the descriptor's depth.
    readonly property real rowRoom: roomAt(2)
    readonly property real valueWanted: valueColumnShown
        ? Math.max(Math.round(Type.bodyPixels * 4.2), Math.min(valueSeen, Math.round(Type.bodyPixels * 16.5), endpointTree.width * 0.4))
        : 0
    // 1. rw gives way first: shown while a depth-3 name keeps 7.2 body heights with it.
    readonly property real accessColumn: roomAt(3) - (accessFull + columnGap) - (typeFull + columnGap)
        - (valueWanted > 0 ? valueWanted + columnGap : 0) >= Type.bodyPixels * 7.2 ? accessFull : 0
    readonly property real accessPart: accessColumn > 0 ? accessColumn + columnGap : 0
    // 2. Value narrows to its least before a name drops below its floor.
    readonly property real valueColumn: valueColumnShown
        ? Math.max(Math.round(Type.bodyPixels * 4.2),
                   Math.min(valueWanted, rowRoom - accessPart - (typeFull + columnGap) - columnGap - Type.nameFloor))
        : 0
    // Previews off: W, always. On: what the names leave, never under three characters.
    readonly property real typeColumn: !valueColumnShown ? typeFull
        : Math.max(Math.ceil(leastTypeMetrics.advanceWidth),
                   Math.min(typeFull, rowRoom - accessPart - (valueColumn + columnGap) - Type.nameFloor - columnGap))
    // The columns' left edges in the tree's coordinates, right to left.
    readonly property real accessLeft: endpointTree.width - scrollInset - accessColumn
    readonly property real typeLeft: accessLeft - (accessColumn > 0 ? columnGap : 0) - typeColumn
    readonly property real valueLeft: typeLeft - (typeColumn > 0 ? columnGap : 0) - valueColumn
    // 4. One indent step for the whole tree (Details' rule): 16 px while a
    // name at the descriptor's depth D keeps 6 body heights (80 px), then
    // evenly less, never under 8 px (VS Code's). D is the deepest group plus
    // 2, a Field and one member level. It follows the descriptor and the
    // width, never what is open, so opening never moves a row sideways.
    readonly property int deepestGroup: searchSource && typeof searchSource.deepestGroup === "number"
                                        ? searchSource.deepestGroup : walkedDeepest
    // A model without deepestGroup: its deepest row that is not an endpoint,
    // found once per reset.
    property int walkedDeepest: 0
    readonly property int indentDepth: Math.max(1, deepestGroup + 2)
    readonly property real indentStep: {
        const nameEnd = (valueColumn > 0 ? valueLeft : typeLeft) - columnGap
        const room = nameEnd - nameXAt(0, 0) - Math.round(Type.bodyPixels * 6)
        return Math.max(Math.floor(Type.indent / 2), Math.min(Type.indent, Math.floor(room / indentDepth)))
    }
    function walkDeepest() {
        const source = searchSource
        if (!source || typeof source.deepestGroup === "number") return
        let deepest = 0
        let budget = groupWalkLimit
        const walk = function(parent, depth) {
            for (let row = 0; row < source.rowCount(parent) && budget > 0; ++row, --budget) {
                const child = source.index(row, 0, parent)
                if (source.rowCount(child) > 0 && !treeFilter.value(child, "isEndpoint") && !treeFilter.value(child, "previewChild")) {
                    deepest = Math.max(deepest, depth)
                    if (depth < searchDepthLimit) walk(child, depth + 1)
                }
            }
        }
        walk(source.index(-1, -1), 0)
        walkedDeepest = deepest
    }
    // A group's count in words, Fields first, as parts that give way from
    // the right ("7 Fields", "1 Command"); a model without the families'
    // counts gives its endpoints' count, by its category.
    function countParts(model) {
        if (typeof model.fieldCount === "number") {
            const parts = []
            if (model.fieldCount > 0) parts.push(fieldsText(model.fieldCount))
            if (model.commandCount > 0) parts.push(model.commandCount === 1 ? qsTr("1 Command") : qsTr("%L1 Commands").arg(model.commandCount))
            if (model.serviceCount > 0) parts.push(model.serviceCount === 1 ? qsTr("1 Service") : qsTr("%L1 Services").arg(model.serviceCount))
            return parts
        }
        const count = typeof model.count === "number" ? model.count : 0
        if (!(count > 0)) return []
        if (model.category === 2) return [count === 1 ? qsTr("1 Command") : qsTr("%L1 Commands").arg(count)]
        if (model.category === 3) return [count === 1 ? qsTr("1 Service") : qsTr("%L1 Services").arg(count)]
        return [fieldsText(count)]
    }
    // The whole count, with the groups under it: "7 Fields · 1 Command in 4 groups".
    function countFull(model) {
        const words = countParts(model).join(" · ")
        const groups = typeof model.groupCount === "number" ? model.groupCount : 0
        if (words.length === 0 || groups <= 0) return words
        return qsTr("%1 in %2").arg(words).arg(groups === 1 ? qsTr("1 group") : qsTr("%L1 groups").arg(groups))
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
    function valuesText(count) {
        return count === 1 ? qsTr("1 value") : qsTr("%L1 values").arg(count)
    }
    // What a partly ticked box counts, in words: "2 of 3 members watched".
    function tallyOf(index, revision) {
        if (!watchApi || !index || !index.valid || typeof provider.watchTally !== "function")
            return null
        const tally = provider.watchTally(index)
        return tally && tally.total > 0 ? tally : null
    }
    // What a click on a struct's or array's box does, counted.
    function tallyHint(tally, state) {
        if (!tally)
            return ""
        if (state === Qt.PartiallyChecked)
            return qsTr("%1. Click to watch all %L2.").arg(tallyText(tally)).arg(tally.total)
        const word = tally.kind === "members" ? qsTr("members") : tally.kind === "elements" ? qsTr("elements")
                   : tally.kind === "fields" ? qsTr("Fields") : qsTr("values")
        return state === Qt.Checked ? qsTr("Click to stop watching all %L1 %2.").arg(tally.total).arg(word)
                                    : qsTr("Click to watch all %L1 %2.").arg(tally.total).arg(word)
    }
    function tallyText(tally) {
        if (!tally)
            return ""
        switch (tally.kind) {
        case "members": return qsTr("%L1 of %L2 members watched").arg(tally.watched).arg(tally.total)
        case "elements": return qsTr("%L1 of %L2 elements watched").arg(tally.watched).arg(tally.total)
        case "fields": return qsTr("%L1 of %L2 Fields watched").arg(tally.watched).arg(tally.total)
        default: return qsTr("%L1 of %L2 values watched").arg(tally.watched).arg(tally.total)
        }
    }
    // A row's path as the Watchlist names it: "device.Config.Gain",
    // "data.Samples[3]"; from the model where it has one.
    function rowPath(index) {
        if (searchSource && typeof searchSource.pathOf === "function") {
            const path = searchSource.pathOf(index)
            if (path.length > 0)
                return path
        }
        return pathOf(index)
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
        // A struct or an array changes many rows and says so, like a group.
        const tally = tallyOf(index, watchRevision)
        // A group by its own name ("signals"), a Field or a value by its path.
        return applyWatch([index], state !== Qt.Checked, group ? nameOf(index) : rowPath(index),
                          group || (tally !== null && tally.total > 1))
    }
    // Watches or stops watching the Fields under indexes, as one change, and
    // says what it did. A single Field says nothing when it worked.
    function applyWatch(indexes, watched, label, group) {
        if (watchLocked) {
            say(pollingShort, pollingFull, false)
            return false
        }
        const result = provider.setWatchMany(indexes, watched)
        const values = result && result.values === true
        if (!result || result.status === "capacity") {
            const adding = !result ? 0 : result.adding !== undefined ? result.adding : result.fields
            const room = result ? result.room : 0
            if (values)
                say(qsTr("No room: %L1 more values, %L2 free").arg(adding).arg(room),
                    qsTr("Watching all of %1 adds %L2 values; the Watchlist has room for %L3 more of its %L4. Remove some first.")
                        .arg(label).arg(adding).arg(room).arg(provider.watchLimit), false)
            else
                say(qsTr("No room: %L1 more Fields, %L2 free").arg(adding).arg(room),
                    qsTr("Watching all of %1 adds %L2 Fields; the Watchlist has room for %L3 more of its %L4. Remove some first.")
                        .arg(label).arg(adding).arg(room).arg(provider.watchLimit), false)
            return false
        }
        if (result.status === "stale" || result.status === "invalid") {
            say(qsTr("Not changed: the tree changed"),
                qsTr("The Device tree changed meanwhile, so the Watchlist is unchanged. Try again."), false)
            return false
        }
        if (result.status !== "ok") {
            say(qsTr("Not changed: %1").arg(result.status),
                qsTr("Could not change the Watchlist: %1.").arg(result.status), false)
            return false
        }
        const serial = typeof result.serial === "number" ? result.serial : 0
        if (!group && result.changed === 0)
            say("", "", false)
        else if (!group && watched)
            say(qsTr("Added %1").arg(label), qsTr("Added %1 to the Watchlist.").arg(label), serial)
        else if (!group)
            say(qsTr("Removed %1").arg(label), qsTr("Removed %1 from the Watchlist.").arg(label), serial)
        else if (result.changed === 0)
            say(watched ? qsTr("Already watched") : qsTr("Nothing watched"),
                watched ? qsTr("Everything in %1 is already watched.").arg(label)
                        : qsTr("Nothing in %1 is watched.").arg(label), false)
        else if (watched)
            say(qsTr("Added %L1").arg(result.changed),
                qsTr("Added %1 from %2 to the Watchlist.")
                    .arg(values ? valuesText(result.changed) : fieldsText(result.changed)).arg(label), serial)
        else
            say(qsTr("Removed %L1").arg(result.changed),
                qsTr("Removed %1 of %2 from the Watchlist.")
                    .arg(values ? valuesText(result.changed) : fieldsText(result.changed)).arg(label), serial)
        return result.changed > 0
    }
    // Says what the last action did, at the revision it left: a short line,
    // and the whole sentence for its tooltip and a screen reader (a status
    // line is not read out by itself; the sentence is); serial is the step it
    // recorded (0: none), which the line offers to undo. Another change of the
    // Watchlist or the tree clears it.
    function say(text, full, serial) {
        watchNotice.say(text, serial, full)
    }

    function clearWatchMarks() {
        ++watchMarkGeneration
        watchMarks = []
        watchNotice.clear()
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
        watchNotice.clear()
    }
    function addMarkedWatches() {
        if (!bulkWatchAvailable || watchMarks.length === 0 || watchLocked) return
        const generation = watchMarkGeneration
        const originalProvider = provider
        const originalSource = searchSource
        const result = originalProvider.addWatchMany(watchMarks.slice())
        if (generation !== watchMarkGeneration || provider !== originalProvider || searchSource !== originalSource)
            return
        if (!result || result.status !== "ok") {
            say("Could not add fields: " + (result && result.status ? result.status : "invalid"), "", 0)
            return
        }
        const summary = "Added " + result.added + "; already watched " + result.duplicate +
                        "; unsupported " + result.unsupported + "; capacity " + result.capacity
        clearWatchMarks()
        say(summary, "", 0)
    }

    // The name of one source index, read through its named role.
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
    // The walk is bounded: depth 64 and 4096 visited rows; a walk cut short
    // says so.
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
        if (watchLocked) {
            say("Stop polling to change the Watchlist.", "", 0)
            return false
        }
        if (indexes.length === 0) {
            say("Nothing in " + label + " can be watched.", "", 0)
            return false
        }
        const originalProvider = provider
        const result = originalProvider.addWatchMany(indexes)
        if (provider !== originalProvider)
            return false
        if (!result || result.status !== "ok") {
            say("Could not watch " + label + ": " + (result && result.status ? result.status : "invalid"), "", 0)
            return false
        }
        if (!group)
            say(result.added === 1 ? "Watching " + label + "." :
                result.duplicate === 1 ? label + " is already watched." :
                result.capacity === 1 ? "The Watchlist is full; " + label + " was not added." :
                label + " cannot be watched.", "", 0)
        else
            say("Watching " + result.added + " from " + label + "; already watched " + result.duplicate +
                "; unsupported " + result.unsupported + "; capacity " + result.capacity +
                (limited ? "; stopped after " + groupWalkLimit + " rows" : ""), "", 0)
        return result.added > 0
    }
    function watchIndex(index) {
        if (watchApi)
            return stateOf(index, watchRevision) >= 0 ? applyWatch([index], true, rowPath(index), false) : false
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
        const path = rowPath(index)
        if (path.length === 0)
            return false
        clipboardHelper.text = path
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.text = ""
        say("Copied " + path + ".", "", 0)
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
    // The most top-level groups a new descriptor opens with (Miller): with
    // more, every branch stays folded for the user to choose.
    readonly property int openLimit: 7
    function openInitialGroups() {
        if (!openOnLoad)
            return
        // A connected model can reset after this panel was created empty.
        // Materialize its new root rows before inspecting the flattened view.
        endpointTree.forceLayout()
        const tops = endpointTree.rows
        if (tops === 0 || tops > openLimit)
            return
        // One level: each top-level group opens, nothing below it. From the
        // last, so the rows above keep their numbers.
        for (let row = tops - 1; row >= 0; --row)
            if (isGroup(sourceOf(endpointTree.index(row, 0))) && !endpointTree.isExpanded(row))
                endpointTree.expand(row)
        endpointTree.forceLayout()
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

    // The tree's rows and the device's: a shown row's index maps to the
    // device's model and back; a row the find leaves out maps to none.
    function sourceOf(shown) {
        return shown && shown.valid ? treeFilter.mapToSource(shown) : treeFilter.mapToSource(treeFilter.index(-1, -1))
    }
    function shownOf(source) {
        return treeFilter.mapFromSource(source && source.valid ? source : treeFilter.index(-1, -1))
    }
    // What a shown row is (the row's kinds); a model without a kind role is
    // worked out as the row does.
    function kindAt(row) {
        const index = endpointTree.index(row, 0)
        const kind = treeFilter.value(index, "kind")
        if (typeof kind === "string" && kind.length > 0)
            return kind
        const endpoint = Boolean(treeFilter.value(index, "isEndpoint"))
        const child = Boolean(treeFilter.value(index, "previewChild"))
        const category = Number(treeFilter.value(index, "category"))
        const type = String(treeFilter.value(index, "typeText") || "")
        return !endpoint && !child ? "group"
            : category === 2 ? "command" : category === 3 ? "service"
            : type.endsWith("]") ? "array" : type.startsWith("struct") ? "struct" : "value"
    }
    function hasChildrenAt(row) {
        return row >= 0 && endpointTree.model.hasChildren(endpointTree.index(row, 0))
    }
    function parentRowOf(row) {
        const depth = endpointTree.depth(row)
        for (let r = row - 1; r >= 0; --r)
            if (endpointTree.depth(r) < depth)
                return r
        return -1
    }
    // Makes a shown row current and brings it into view.
    function setCurrentRow(row) {
        if (row < 0)
            return
        endpointTree.forceLayout()
        if (row >= endpointTree.rows)
            return
        endpointTree.selectionModel.setCurrentIndex(endpointTree.index(row, 0), ItemSelectionModel.NoUpdate)
        endpointTree.positionViewAtRow(row, TableView.Contain)
    }
    // Brings a device row into view: only the path to it opens, nothing
    // closes (design G). current: it becomes the keyboard's row too.
    function reveal(index, current) {
        const shown = shownOf(index)
        if (!shown.valid)
            return false
        endpointTree.expandToIndex(shown)
        endpointTree.forceLayout()
        const row = endpointTree.rowAtIndex(shown)
        if (row < 0)
            return false
        if (current)
            endpointTree.selectionModel.setCurrentIndex(shown, ItemSelectionModel.NoUpdate)
        endpointTree.positionViewAtRow(row, TableView.Contain)
        return true
    }
    // Opens every container under a row (*): a group and every group under
    // it, its Fields folded; a struct or an array Field all its values,
    // within the model's bound of 4,096 rows. A struct or array under a group
    // stays folded, so * on a group never buries the tree in its elements.
    function expandUnder(row) {
        if (row < 0 || row >= endpointTree.rows)
            return
        const kind = kindAt(row)
        if (kind === "struct" || kind === "array") {
            endpointTree.expandRecursively(row, -1)
            return
        }
        if (!hasChildrenAt(row))
            return
        const base = endpointTree.depth(row)
        endpointTree.expand(row)
        endpointTree.forceLayout()
        for (let r = row + 1; ; ++r) {
            if (r >= endpointTree.rows) {
                endpointTree.forceLayout()
                if (r >= endpointTree.rows)
                    break
            }
            if (endpointTree.depth(r) <= base)
                break
            const inner = kindAt(r)
            if ((inner === "category" || inner === "group") && !endpointTree.isExpanded(r) && hasChildrenAt(r))
                endpointTree.expand(r)
        }
        endpointTree.forceLayout()
    }
    function collapseUnder(row) {
        if (row >= 0 && row < endpointTree.rows)
            endpointTree.collapseRecursively(row)
    }
    // Every branch folded: the top-level groups alone.
    function collapseAll() {
        endpointTree.collapseRecursively()
    }
    // The keys of the tree (design G): true when the key was taken.
    function treeKey(event) {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const plain = !ctrl && !alt
        // Ctrl+F finds, as in the file views; a host's Ctrl+K too.
        if (event.key === Qt.Key_F && ctrl) {
            find()
            return true
        }
        // F3 and Shift+F3 step through the matches from the tree.
        if (event.key === Qt.Key_F3 && plain)
            return stepFind(shift ? -1 : 1)
        // Home and End: the first and the last row, in view.
        if ((event.key === Qt.Key_Home || event.key === Qt.Key_End) && plain) {
            endpointTree.forceLayout()
            if (endpointTree.rows === 0)
                return false
            setCurrentRow(event.key === Qt.Key_Home ? 0 : endpointTree.rows - 1)
            return true
        }
        const index = endpointTree.selectionModel.currentIndex
        const row = index.valid ? endpointTree.rowAtIndex(index) : -1
        const source = sourceOf(index)
        // Ctrl+C and Alt+Shift+C copy the current row's path, as
        // values.bin copies its rows; values are copied where shown.
        if (event.key === Qt.Key_C && (event.modifiers === Qt.ControlModifier
                                       || event.modifiers === (Qt.AltModifier | Qt.ShiftModifier)))
            return source.valid && copyPath(source)
        // The Menu key or Shift+F10 opens the actions of the current row.
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && shift)) {
            if (row < 0)
                return false
            // A row scrolled out of view has no delegate until shown.
            let cell = endpointTree.itemAtIndex(index)
            if (!cell) {
                endpointTree.positionViewAtIndex(index, TableView.Contain)
                endpointTree.forceLayout()
                cell = endpointTree.itemAtIndex(index)
            }
            return !!cell && openEndpointMenu(source, cell.isEndpoint, cell)
        }
        if (row < 0)
            return false
        const branch = hasChildrenAt(row)
        const open = endpointTree.isExpanded(row)
        // Right opens a folded branch, and on an open one steps to its first
        // child; Left closes an open branch, else steps to the parent.
        if (event.key === Qt.Key_Right && plain && !shift) {
            if (branch && !open)
                endpointTree.expand(row)
            else if (branch && open)
                setCurrentRow(row + 1)
            return true
        }
        if (event.key === Qt.Key_Left && plain && !shift) {
            if (branch && open)
                endpointTree.collapse(row)
            else
                setCurrentRow(parentRowOf(row))
            return true
        }
        if (event.key === Qt.Key_Asterisk || event.text === "*") {
            expandUnder(row)
            return true
        }
        if (event.key === Qt.Key_Minus || event.text === "-") {
            collapseUnder(row)
            return true
        }
        // Enter on a group folds it; on a Field, a Command or a Service it
        // opens Details.
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && plain) {
            const kind = kindAt(row)
            if ((kind === "category" || kind === "group") && branch)
                endpointTree.toggleExpanded(row)
            else
                requestSelection(source)
            return true
        }
        // Space watches the current row, as its box does.
        if (event.key === Qt.Key_Space && plain) {
            if (watchApi) {
                if (stateOf(source, watchRevision) >= 0) {
                    toggleWatch(source)
                    return true
                }
                const refusal = refusalOf(source, watchRevision)
                if (refusal.length > 0) {
                    say(refusal, refusal, false)
                    return true
                }
            } else if (canMarkWatch(source)) {
                toggleWatchMark(source)
                return true
            }
        }
        return false
    }

    // Find (design G1, the review's 14.1): the tree filtered in place - the
    // matches and their parents, the parents open, the matches folded - and
    // Enter stepping through them. Groups, Fields, Commands and Services are
    // found by name, by path ("/" and "." one separator, the segments in
    // order with gaps: "harm/H3") or by a named type; the values inside a
    // Field are not. The kinds filter narrows it in place, with or without text.
    // What the tree is filtered by: the field's text.
    property string findText: ""
    // The kinds shown (TreeFilter's bits: Fields 1, Commands 2, Services 4);
    // 0 shows every kind.
    property int kindsShown: 0
    readonly property bool kindsOn: kindsShown !== 0 && kindsShown !== 7
    readonly property bool finding: findText.length > 0 || kindsOn
    readonly property int findCount: treeFilter.count
    // The match made current last by Enter or F3, from 0; -1 before any.
    property int findStep: -1
    // "Commands only", "Fields and Services only": what the funnel shows.
    readonly property string kindsWords: {
        const names = []
        if (kindsShown & 1) names.push(qsTr("Fields"))
        if (kindsShown & 2) names.push(qsTr("Commands"))
        if (kindsShown & 4) names.push(qsTr("Services"))
        return !kindsOn ? "" : names.length === 1 ? qsTr("%1 only").arg(names[0])
                                               : qsTr("%1 and %2 only").arg(names[0]).arg(names[1])
    }
    readonly property string matchWords: findCount === 0 && findText.length > 0 ? qsTr("Nothing matches “%1”.").arg(searchInput.text.trim())
        : findCount === 0 ? qsTr("No matches")
        : findStep >= 0 && findStep < findCount ? qsTr("%L1 of %L2 matches").arg(findStep + 1).arg(findCount)
        : findCount === 1 ? qsTr("1 match") : qsTr("%L1 matches").arg(findCount)
    readonly property string findStatus: !finding ? ""
        : kindsOn ? qsTr("%1 · %2").arg(kindsWords).arg(matchWords) : matchWords
    onFindCountChanged: if (findStep >= findCount) findStep = -1
    // A kind on or off; all three on is the tree unfiltered.
    function toggleKind(bit) {
        const next = (kindsOn ? kindsShown : 0) ^ bit
        kindsShown = next === 7 ? 0 : next
    }
    onKindsShownChanged: {
        const was = treeFilter.active
        if (!was && kindsOn)
            snapshotFolds()
        const kept = was && !kindsOn && findText.length === 0
                     ? treeFilter.namePath(endpointTree.selectionModel.currentIndex) : ""
        findStep = -1
        treeFilter.kinds = kindsShown
        if (treeFilter.active) {
            showFound()
            return
        }
        endFiltering(kept)
    }
    function applyFind() {
        findTimer.stop()
        const needle = searchInput.text.trim()
        if (needle === findText)
            return
        const was = treeFilter.active
        if (needle.length > 0 && !was)
            snapshotFolds()
        // The row the keyboard is on, kept through the end of a find.
        const kept = was && needle.length === 0 && !kindsOn ? treeFilter.namePath(endpointTree.selectionModel.currentIndex) : ""
        findStep = -1
        findText = needle
        treeFilter.setText(needle)
        if (treeFilter.active) {
            showFound()
            return
        }
        endFiltering(kept)
    }
    // The tree unfiltered again: what was open before the find, then the
    // path to the row the keyboard was on (kept), which stays current and in view.
    function endFiltering(kept) {
        const memory = foldMemory[descriptorKey]
        restoreFolds(memory ? memory.open : [], true)
        // A descriptor that came during the find opens as a new one does.
        if (!memory)
            openInitialGroups()
        if (kept.length > 0)
            revealKey(kept, true)
        snapshotFolds()
    }
    // The matches' parents open, the matches folded.
    function showFound() {
        let matchDepth = -1
        for (let row = 0; ; ++row) {
            if (row >= endpointTree.rows) {
                endpointTree.forceLayout()
                if (row >= endpointTree.rows)
                    break
            }
            const depth = endpointTree.depth(row)
            if (matchDepth >= 0 && depth > matchDepth)
                continue
            matchDepth = -1
            if (treeFilter.matches(endpointTree.index(row, 0))) {
                matchDepth = depth
                if (endpointTree.isExpanded(row))
                    endpointTree.collapse(row)
            } else if (!endpointTree.isExpanded(row) && hasChildrenAt(row)) {
                endpointTree.expand(row)
            }
        }
        endpointTree.forceLayout()
    }
    // The next (1) or the previous (-1) match becomes current and selected,
    // in view, wrapping; Details shows it. False with nothing to step to.
    function stepFind(delta) {
        if (findTimer.running || searchInput.text.trim() !== findText)
            applyFind()
        if (!finding)
            return false
        const list = treeFilter.found()
        if (list.length === 0)
            return false
        findStep = findStep < 0 ? (delta > 0 ? 0 : list.length - 1)
                                : (findStep + delta + list.length) % list.length
        const shown = list[findStep]
        // A match under a branch folded since, or rows come after the find.
        endpointTree.expandToIndex(shown)
        endpointTree.forceLayout()
        const row = endpointTree.rowAtIndex(shown)
        if (row < 0)
            return false
        setCurrentRow(row)
        const kind = kindAt(row)
        if (kind !== "category" && kind !== "group")
            requestSelection(sourceOf(shown))
        return true
    }

    // What is open survives a reconnect (design G): the open branches of
    // each descriptor seen in this session, by the names from the root
    // ("device/Config"), and the keyboard's row. A descriptor is told by its
    // top-level groups' names and their rows'. A descriptor never seen opens
    // as openOnLoad says; one seen before opens as it was left. A find's own
    // opening is never kept.
    property var foldMemory: ({})
    property string descriptorKey: ""
    property bool foldBusy: false
    function identityOf() {
        const source = treeFilter.sourceModel
        if (!source)
            return ""
        const parts = []
        for (let r = 0; r < source.rowCount(); ++r) {
            const top = source.index(r, 0)
            const names = []
            for (let c = 0; c < source.rowCount(top) && c < 1024; ++c)
                names.push(treeFilter.nameOf(source.index(c, 0, top)))
            parts.push(treeFilter.nameOf(top) + ":" + names.join(","))
        }
        return parts.join(";")
    }
    // The device's row at such a path: the device's model finds its own;
    // any other through the filter, by names.
    function sourceAtKey(key) {
        const source = treeFilter.sourceModel
        if (!source || key.length === 0)
            return treeFilter.mapToSource(treeFilter.index(-1, -1))
        return typeof source.indexOfPath === "function" && typeof source.widestFieldType === "string"
            ? source.indexOfPath(key) : treeFilter.sourceAt(key)
    }
    function revealKey(key, current) {
        const index = sourceAtKey(key)
        return index && index.valid ? reveal(index, current) : false
    }
    // Records what is open now: every open branch shown, and those kept under
    // a folded one (the tree opens them again with it).
    function snapshotFolds() {
        snapshotTimer.stop()
        if (finding || foldBusy || descriptorKey.length === 0)
            return
        const memory = foldMemory[descriptorKey] || { open: [], current: "" }
        endpointTree.forceLayout()
        const open = []
        const shown = {}
        for (let row = 0; row < endpointTree.rows; ++row) {
            const key = treeFilter.namePath(endpointTree.index(row, 0))
            shown[key] = true
            if (endpointTree.isExpanded(row))
                open.push(key)
        }
        for (const key of memory.open)
            if (!shown[key])
                open.push(key)
        memory.open = open
        foldMemory[descriptorKey] = memory
    }
    // A recursive fold forgets everything under the row; folding all, all.
    function forgetUnder(row) {
        const memory = foldMemory[descriptorKey]
        if (finding || foldBusy || !memory)
            return
        if (row < 0) {
            memory.open = []
            return
        }
        const key = treeFilter.namePath(endpointTree.index(row, 0))
        memory.open = memory.open.filter(function(item) { return item !== key && !item.startsWith(key + "/") })
    }
    function noteCurrent() {
        const memory = foldMemory[descriptorKey]
        const key = treeFilter.namePath(endpointTree.selectionModel.currentIndex)
        // A current row lost to a reset or a find is not the user's choice.
        if (foldBusy || descriptorKey.length === 0 || key.length === 0)
            return
        if (memory)
            memory.current = key
        else
            foldMemory[descriptorKey] = { open: [], current: key }
    }
    // Opens the branches at keys, shallowest first; collapse: fold all first.
    function restoreFolds(keys, collapse) {
        foldBusy = true
        if (collapse)
            endpointTree.collapseRecursively()
        const sorted = keys.slice().sort(function(a, b) { return a.split("/").length - b.split("/").length })
        for (const key of sorted) {
            const shown = shownOf(sourceAtKey(key))
            const row = shown.valid ? endpointTree.rowAtIndex(shown) : -1
            if (row >= 0 && !endpointTree.isExpanded(row))
                endpointTree.expand(row)
        }
        endpointTree.forceLayout()
        foldBusy = false
    }
    // After the model is made anew (a connect, a reconnect) or at the start.
    function afterReset() {
        endpointTree.forceLayout()
        walkDeepest()
        descriptorKey = identityOf()
        if (descriptorKey.length === 0)
            return
        if (finding) {
            showFound()
            return
        }
        const memory = foldMemory[descriptorKey]
        if (memory) {
            restoreFolds(memory.open, false)
            if (memory.current.length > 0)
                revealKey(memory.current, true)
        } else {
            openInitialGroups()
        }
        const selected = panel.selectedEndpointIndex
        if (selected && selected.valid && !selectionStale)
            reveal(selected, !memory || memory.current.length === 0)
        snapshotFolds()
    }
    // A provider's selection made before the model was reset: an index into
    // rows that are gone, never mapped (a plain index of a deleted row reads
    // freed memory). It is fresh again when the provider selects anew.
    property bool selectionStale: false

    // The current branch's guide (design E): the current row if it is an open
    // branch, else its parent; the rows below it at that level.
    property int guideLevel: -1
    property int guideFrom: 0
    property int guideTo: -1
    function updateGuides() {
        const index = endpointTree.selectionModel.currentIndex
        const row = index.valid ? endpointTree.rowAtIndex(index) : -1
        if (row < 0 || row >= endpointTree.rows) {
            guideLevel = -1
            return
        }
        let branch = row
        if (!(hasChildrenAt(row) && endpointTree.isExpanded(row))) {
            branch = endpointTree.depth(row) === 0 ? -1 : parentRowOf(row)
            if (branch < 0) {
                guideLevel = -1
                return
            }
        }
        const level = endpointTree.depth(branch)
        let last = branch
        for (let r = branch + 1; r < endpointTree.rows && endpointTree.depth(r) > level; ++r)
            last = r
        guideFrom = branch + 1
        guideTo = last
        guideLevel = last > branch ? level : -1
    }

    // The context line (the review's 14.1, the kinds design's sticky
    // headers): the path of the group the top row in view belongs to, laid
    // over the tree under its header once the tree is scrolled - the group's
    // own row is then at or above the line, which stands for it. It never
    // covers the current row. A click on a segment makes that group current;
    // Left reaches it from the keyboard.
    property string contextPath: ""
    function groupPathAt(row) {
        const index = endpointTree.index(row, 0)
        const path = treeFilter.value(index, "path")
        return typeof path === "string" && path.length > 0 ? path
            : treeFilter.namePath(index).split("/").map(function(name) { return name.replace(/([\/.\[\]\\])/g, "\\$1") }).join("/")
    }
    function updateContext() {
        const rh = Type.rowHeight
        const rows = endpointTree.rows
        const y = endpointTree.contentY
        if (rows === 0 || y <= 0.5) {
            contextPath = ""
            return
        }
        // The first row below the line, and the group it belongs to.
        const top = Math.min(rows - 1, Math.floor((y + rh) / rh))
        let group = -1
        for (let r = parentRowOf(top); r >= 0; r = parentRowOf(r))
            if (kindAt(r) === "group") {
                group = r
                break
            }
        const index = endpointTree.selectionModel.currentIndex
        const current = index.valid ? endpointTree.rowAtIndex(index) : -1
        const coversCurrent = current >= 0 && current * rh < y + rh && (current + 1) * rh > y
        contextPath = group >= 0 && !coversCurrent ? groupPathAt(group) : ""
    }
    // Makes a group current and shows it: a context line's or a breadcrumb's
    // segment, by the group's path in the one grammar.
    function revealGroupPath(path) {
        const source = treeFilter.sourceModel
        if (!source || path.length === 0)
            return false
        let index = typeof source.indexOfGroupPath === "function" ? source.indexOfGroupPath(path) : null
        if (!index || !index.valid)
            index = treeFilter.sourceAt(path.replace(/\\(.)/g, "$1"))
        return index && index.valid ? reveal(index, true) : false
    }
    Timer { id: contextTimer; interval: 0; onTriggered: panel.updateContext() }
    // The device's totals of each kind for the funnel's menu; -1 unknown.
    function kindTotal(bit) {
        const source = searchSource
        const value = !source ? undefined : bit === 1 ? source.fieldTotal : bit === 2 ? source.commandTotal : source.serviceTotal
        return typeof value === "number" ? value : -1
    }
    // A provider may ask every tree to show a group (Details' breadcrumb).
    Connections {
        target: panel.provider
        ignoreUnknownSignals: true
        function onRevealGroupRequested(path) { panel.revealGroupPath(path) }
    }

    onProviderChanged: { clearPreviews(); clearWatchMarks() }
    onSearchSourceChanged: { clearPreviews(); clearWatchMarks(); afterResetTimer.restart() }
    // A selection made elsewhere (the Watchlist, the Batch, a host's search)
    // is brought into view, and the keyboard's row follows it.
    onSelectedEndpointIndexChanged: {
        selectionStale = false
        revealSelection.restart()
    }
    Timer {
        id: revealSelection
        interval: 0
        onTriggered: {
            const index = panel.selectedEndpointIndex
            if (index && index.valid && !panel.selectionStale)
                panel.reveal(index, true)
        }
    }
    TreeFilter {
        id: treeFilter
        objectName: "endpointTreeFilter"
        sourceModel: panel.searchSource ? panel.searchSource : null
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
        // The menu's row in the tree, or -1.
        function targetRow() {
            const target = panel.menuTarget()
            const shown = target ? panel.shownOf(target) : null
            return shown && shown.valid ? endpointTree.rowAtIndex(shown) : -1
        }
        onClosed: {
            const item = returnFocus
            returnFocus = null
            if (item && item.visible && !item.activeFocus)
                item.forceActiveFocus()
        }
        MenuItem {
            objectName: "endpointMenuWatch"
            // A Field or a value inside one: what its box watches.
            readonly property int boxState: panel.watchApi && endpointMenu.target && !endpointMenu.targetIsGroup
                                            ? panel.stateOf(endpointMenu.target, panel.watchRevision) : -1
            text: boxState === Qt.Checked ? qsTr("Stop watching") : qsTr("Watch")
            enabled: !panel.watchLocked &&
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
            enabled: panel.bulkWatchAvailable && endpointMenu.targetIsGroup && !panel.watchLocked &&
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
            enabled: panel.bulkWatchAvailable && !panel.watchLocked
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
            text: "Copy path\tCtrl+C"
            onTriggered: { const target = panel.menuTarget(); if (target) panel.copyPath(target) }
        }
        // Folding, as Details' menu has it, with the keys beside.
        MenuSeparator {}
        MenuItem {
            objectName: "treeMenuExpandAll"
            text: qsTr("Expand all under\t*")
            enabled: endpointMenu.targetRow() >= 0
            onTriggered: panel.expandUnder(endpointMenu.targetRow())
        }
        MenuItem {
            objectName: "treeMenuCollapseUnder"
            text: qsTr("Collapse all under\t-")
            enabled: endpointMenu.targetRow() >= 0
            onTriggered: panel.collapseUnder(endpointMenu.targetRow())
        }
        MenuItem {
            objectName: "treeMenuCollapseAll"
            text: qsTr("Collapse all")
            onTriggered: panel.collapseAll()
        }
    }
    // Typing settles before the tree is filtered.
    Timer { id: findTimer; interval: 50; onTriggered: panel.applyFind() }
    Timer { id: snapshotTimer; interval: 0; onTriggered: panel.snapshotFolds() }
    Timer { id: guideTimer; interval: 0; onTriggered: panel.updateGuides() }
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
    // every branch for the user to open sets it false. A descriptor seen
    // before in this session opens as it was left instead.
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
                enabled: panel.watchMarks.length > 0 && !panel.watchLocked
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
            visible: endpointTree.rows > 0 || searchInput.text.length > 0 || panel.kindsOn
            spacing: Type.spacing
            TextField {
                id: searchInput
                objectName: "endpointSearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Find by name, path or type (Ctrl+F)")
                maximumLength: 256
                // Room for the clear button inside the field.
                rightPadding: searchClear.visible ? searchClear.width + 4 : leftPadding
                onTextChanged: findTimer.restart()
                // Esc clears the find: the tree as it was, the current row kept.
                Keys.onEscapePressed: function(event) {
                    if (text.length === 0)
                        return
                    clear()
                    panel.applyFind()
                    event.accepted = true
                }
                // Enter steps to the next match, Shift+Enter to the previous;
                // with nothing to step to, the key goes on to the host (a
                // default button). Space or a row's box watches.
                Keys.onReturnPressed: function(event) {
                    event.accepted = panel.stepFind((event.modifiers & Qt.ShiftModifier) ? -1 : 1)
                }
                Keys.onEnterPressed: function(event) {
                    event.accepted = panel.stepFind((event.modifiers & Qt.ShiftModifier) ? -1 : 1)
                }
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_F3)
                        event.accepted = panel.stepFind((event.modifiers & Qt.ShiftModifier) ? -1 : 1)
                }
                // Down moves into the tree, at the current match.
                Keys.onDownPressed: function(event) {
                    if (findTimer.running)
                        panel.applyFind()
                    if (panel.finding && panel.findCount > 0) {
                        const list = treeFilter.found()
                        const shown = list[Math.max(0, Math.min(panel.findStep, list.length - 1))]
                        const row = shown ? endpointTree.rowAtIndex(shown) : -1
                        if (row >= 0)
                            panel.setCurrentRow(row)
                    }
                    if (endpointTree.visible)
                        endpointTree.forceActiveFocus()
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
                    onClicked: { searchInput.clear(); panel.applyFind(); searchInput.forceActiveFocus() }
                    Accessible.name: qsTr("Clear the search")
                }
            }
            // The kinds filter (the review's 14.1): one Tab stop after the
            // find field. Its menu holds the device's totals the category
            // rows used to show; a kind chosen narrows the tree in place, as
            // find does, and with it.
            IconToolButton {
                id: kindsButton
                objectName: "endpointKindsFilter"
                iconName: "filter"
                switchedOn: panel.kindsOn
                text: panel.kindsOn ? qsTr("Kinds shown: %1").arg(panel.kindsWords) : qsTr("Show only some kinds")
                onClicked: kindsMenu.open()
                Accessible.role: Accessible.ButtonMenu
                Menu {
                    id: kindsMenu
                    objectName: "endpointKindsMenu"
                    y: kindsButton.height
                    KindsMenuItem { objectName: "endpointKindsFields"; word: qsTr("Fields"); total: panel.kindTotal(1)
                                    checked: panel.kindsOn && (panel.kindsShown & 1) !== 0; onTriggered: panel.toggleKind(1) }
                    KindsMenuItem { objectName: "endpointKindsCommands"; word: qsTr("Commands"); total: panel.kindTotal(2)
                                    checked: panel.kindsOn && (panel.kindsShown & 2) !== 0; onTriggered: panel.toggleKind(2) }
                    KindsMenuItem { objectName: "endpointKindsServices"; word: qsTr("Services"); total: panel.kindTotal(4)
                                    checked: panel.kindsOn && (panel.kindsShown & 4) !== 0; onTriggered: panel.toggleKind(4) }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "endpointKindsAll"
                        text: qsTr("Show all")
                        enabled: panel.kindsOn
                        onTriggered: panel.kindsShown = 0
                    }
                }
            }
            // Values: off | Every 1 s | Every 2 s | Every 5 s | Read now (PV-2).
            Button {
                id: previewToggle
                objectName: "endpointPreviewValues"
                visible: panel.previewsSupported
                flat: true
                text: panel.previewValues ? qsTr("Values: every %1").arg(panel.previewIntervalText(panel.previewIntervalMs))
                                          : qsTr("Values: off")
                onClicked: previewMenu.open()
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: text
                Accessible.description: panel.previewValues
                    ? qsTr("Reads the Fields in view together every %1, after the Watchlist and your own reads. Doesn't change the Watchlist.")
                          .arg(panel.previewIntervalText(panel.previewIntervalMs))
                    : qsTr("Fields in view show no values. Choose how often to read them together; the Watchlist is not changed.")
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: Accessible.description
                Menu {
                    id: previewMenu
                    objectName: "endpointPreviewMenu"
                    y: previewToggle.height
                    MenuItem {
                        objectName: "endpointPreviewOff"
                        text: qsTr("Off")
                        checkable: true
                        checked: !panel.previewValues
                        onTriggered: panel.previewValues = false
                    }
                    PreviewEvery { ms: 1000 }
                    PreviewEvery { ms: 2000 }
                    PreviewEvery { ms: 5000 }
                    MenuSeparator {}
                    MenuItem {
                        objectName: "endpointPreviewReadNow"
                        text: qsTr("Read now")
                        enabled: panel.previewValues && typeof panel.provider.readPreviewsNow === "function"
                        onTriggered: panel.provider.readPreviewsNow()
                    }
                }
            }
        }
        // What the find found: "4 matches", "2 of 4 matches"; nothing found
        // says so and offers the way back (family A11).
        RowLayout {
            Layout.fillWidth: true
            visible: panel.finding
            spacing: Type.spacing
            Label {
                objectName: "endpointSearchStatus"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                font.pointSize: Type.caption
                // Secondary text; a host's own palette text where it set one.
                color: Qt.colorEqual(panel.palette.text, Theme.textPrimary) ? Theme.textSecondary : panel.palette.text
                text: panel.findStatus
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            LinkButton {
                objectName: "endpointSearchClearLink"
                visible: panel.finding && panel.findCount === 0
                text: qsTr("Clear the search")
                font.pointSize: Type.caption
                // The way back is the whole tree: the text and the kinds.
                onClicked: { searchInput.clear(); panel.kindsShown = 0; panel.applyFind(); searchInput.forceActiveFocus() }
            }
        }
        Item {
            objectName: "endpointColumnHeaders"
            visible: endpointTree.rows > 0
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
            // A mouse drag takes a row to the Watchlist, the Plot or the Batch
            // (LP-1g3); the wheel and the scroll bar scroll.
            acceptedButtons: Qt.NoButton
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: treeFilter
            onTopRowChanged: panel.suspendPreviews()
            onBottomRowChanged: panel.suspendPreviews()
            onContentYChanged: contextTimer.restart()
            onExpanded: {
                panel.suspendPreviews()
                guideTimer.restart()
                contextTimer.restart()
                if (!panel.foldBusy && !panel.finding)
                    snapshotTimer.restart()
            }
            onCollapsed: function(row, recursively) {
                panel.suspendPreviews()
                guideTimer.restart()
                contextTimer.restart()
                if (recursively)
                    panel.forgetUnder(row)
                if (!panel.foldBusy && !panel.finding)
                    snapshotTimer.restart()
            }
            onRowsChanged: { guideTimer.restart(); contextTimer.restart() }
            // Current keyboard row is a navigation cursor, not operation
            // selection. Only an explicit click/Enter asks the provider.
            activeFocusOnTab: true
            selectionModel: ItemSelectionModel { model: endpointTree.model }
            Connections {
                target: endpointTree.selectionModel
                function onCurrentChanged() {
                    guideTimer.restart()
                    contextTimer.restart()
                    panel.noteCurrent()
                }
            }
            Keys.onPressed: function(event) {
                if (panel.treeKey(event))
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
                // Dragged: the endpoint's path, or a group's endpoints.
                property var dragged: []
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction
                Drag.active: endpointDrag.active
                Drag.mimeData: ({"application/x-telemetry-paths": JSON.stringify({from: "device", paths: endpointDelegate.dragged}),
                                "text/plain": endpointDelegate.dragged.join("\n")})
                // The paths are taken at the press, before the drag starts.
                PointHandler {
                    acceptedButtons: Qt.LeftButton
                    onActiveChanged: if (active) endpointDelegate.dragged = panel.dragPaths(sourceIndex)
                }
                DragHandler {
                    id: endpointDrag
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                }
                tableMode: true
                nameColumnWidth: endpointTree.width
                valueColumnWidth: panel.valueColumn
                typeColumnWidth: panel.typeColumn
                accessColumnWidth: panel.accessColumn
                columnGap: panel.columnGap
                rightClearance: panel.scrollInset
                kind: typeof model.kind === "string" ? model.kind : ""
                category: typeof model.category === "number" ? model.category : 0
                countParts: !isEndpoint && !previewChild ? panel.countParts(model) : []
                countFull: !isEndpoint && !previewChild ? panel.countFull(model) : ""
                indentStep: panel.indentStep
                pathText: typeof model.path === "string" && model.path.length > 0 ? model.path
                        : treeFilter.namePath(sourceIndex, "/")
                typeNames: typeof model.typeNames === "string" ? model.typeNames : ""
                matchRun: {
                    void name
                    void depth
                    void endpointTree.rows
                    void panel.findText
                    void panel.kindsShown
                    return panel.finding ? treeFilter.run(endpointTree.index(row, 0)) : ({})
                }
                guideLevel: panel.guideLevel
                guideFrom: panel.guideFrom
                guideTo: panel.guideTo
                watchRefusal: panel.refusalOf(sourceIndex, panel.watchRevision)
                onMeasured: function(value, type) { panel.noteMeasured(value, type) }
                typeText: typeof model.typeText === "string" ? model.typeText : ""
                accessText: typeof model.accessText === "string" ? model.accessText : ""
                // Every row of a Field shows its access, its values included
                // (the owner, 20 November 2026).
                accessMark: (isEndpoint || previewChild) && model.category === 1 &&
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
                // A struct's, an array's or a group's box counts its values;
                // its tooltip says what a click does before it adds hundreds.
                readonly property var boxTally: boxState >= 0 && (boxState === Qt.PartiallyChecked || model.previewAggregate === true)
                    ? panel.tallyOf(sourceIndex, panel.watchRevision) : null
                watchTally: boxState === Qt.PartiallyChecked ? panel.tallyText(boxTally) : ""
                watchTallyHint: panel.tallyHint(boxTally, boxState)
                // A row the Watchlist cannot hold yet keeps a box, off, with its reason.
                watchable: panel.watchApi ? boxState >= 0 || watchRefusal.length > 0 : panel.canMarkWatch(sourceIndex)
                watchState: panel.watchApi ? Math.max(0, boxState)
                          : panel.isWatchMarked(sourceIndex) ? Qt.Checked : Qt.Unchecked
                watchEnabled: !panel.watchApi || !panel.watchLocked
                watchOffReason: panel.pollingFull
                hint: detail
                watchName: !panel.watchApi ? "Select " + name + " for Watchlist"
                         : model.previewChild === true ? qsTr("Watch %1").arg(panel.rowPath(sourceIndex))
                         : hasChildren && !isEndpoint ? qsTr("Watch every Field in %1").arg(name)
                         : qsTr("Watch %1").arg(name)
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
            // The context line, laid over the top row under the header: the
            // folder, then the group's path, each segment a link; a 1 px rule
            // under it. Opaque, so the row under it does not show through.
            Rectangle {
                id: contextLine
                objectName: "endpointTreeContext"
                parent: endpointTree
                z: 2
                visible: panel.contextPath.length > 0
                width: endpointTree.width - panel.scrollInset
                height: Type.rowHeight
                color: panel.palette.base
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("In %1").arg(contextLabel.spokenName)
                // A click on the line never reaches the row under it.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    Accessible.ignored: true
                }
                KindIcon {
                    x: panel.nameXAt(0, 0) - Type.iconSize - Type.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "group"
                }
                PathLabel {
                    id: contextLabel
                    objectName: "endpointTreeContextPath"
                    x: panel.nameXAt(0, 0)
                    width: Math.max(0, parent.width - x - Type.padding)
                    anchors.verticalCenter: parent.verticalCenter
                    path: panel.contextPath
                    interactive: true
                    tabStop: false
                    leafLink: true
                    font: panel.font
                    leafWeight: Type.headingWeight
                    onSegmentActivated: function(groupPath) { panel.revealGroupPath(groupPath) }
                }
                Rectangle {
                    objectName: "endpointTreeContextRule"
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.outlineVariant
                    Accessible.ignored: true
                }
            }
            Component.onCompleted: afterResetTimer.restart()
        }
        // What the last watch action did, on one line kept at the bottom: always
        // reserved, so nothing moves when it speaks; blank when idle. The whole
        // of a cut line on hover. Undo follows the words it undoes: "Added 4 ·
        // Undo". While the Watchlist polls, why the boxes are off stands in
        // over the last message; that comes back when polling stops.
        // A fixed height: the tree above takes every other pixel.
        Item {
            id: statusRow
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: Math.max(statusMetrics.height + 4, 24)
            Layout.maximumHeight: Layout.preferredHeight
            FontMetrics { id: statusMetrics; font.pointSize: Type.caption }
            UndoNotice {
                id: watchNotice
                objectName: "endpointWatchSelectionStatus"
                anchors.fill: parent
                // Reserved, blank when idle.
                visible: true
                history: panel.history
                revision: panel.watchApi ? panel.watchRevision : -1
                color: panel.palette.text
                standIn: panel.watchApi && panel.watchLocked ? panel.pollingShort : ""
                standInDetail: panel.pollingFull
            }
        }
    }
    // After a reset, on the next turn of the event loop: the open branches
    // kept for this descriptor, else the defaults. A timer of the panel's own
    // dies with it; a callLater closure outlived an unloaded panel and ran in
    // its destroyed context.
    Timer {
        id: afterResetTimer
        interval: 0
        onTriggered: panel.afterReset()
    }
    Connections {
        target: panel.provider.endpoints
        ignoreUnknownSignals: true
        function onModelAboutToBeReset() {
            panel.clearPreviews()
            panel.clearWatchMarks()
            afterResetTimer.stop()
            revealSelection.stop()
            panel.selectionStale = true
        }
        function onModelReset() {
            panel.forgetMeasured()
            panel.findStep = -1
            afterResetTimer.restart()
        }
        function onRowsAboutToBeRemoved() { panel.clearPreviews(); panel.clearWatchMarks() }
        function onRowsAboutToBeInserted() { panel.clearPreviews(); panel.clearWatchMarks() }
        function onRowsAboutToBeMoved() { panel.clearPreviews(); panel.clearWatchMarks() }
        function onLayoutAboutToBeChanged() { panel.clearPreviews(); panel.clearWatchMarks() }
        function onDestroyed() { panel.clearPreviews(); panel.clearWatchMarks() }
    }
}
