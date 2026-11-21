// The Watchlist: the watched values and their latest readings. One toolbar
// row - Read once, Start, the interval and its pace, List | Tree, the number
// format and a ⋯ menu - then one line for what was done last, the column
// titles and the rows: one per value with its whole path, or a tree grouped by
// path with each kind's glyph and indent guides, as Live Expressions shows
// them. The toolbar and the titles stay where they are; only the rows scroll.
// As the panel narrows, the pace leaves the bar first, then "Every", then Read
// once and List | Tree move into ⋯ (WL-9, the designer's of 20 November 2026).
// A value is written in its row; a text the device refuses, or that is no
// value, stays held in it, struck, with what the device holds under it.
// Presentation only: the caller owns the provider and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Templates as T

Pane {
    id: panel
    required property var provider
    // No flashes and no moving progress line: the words still change.
    property bool reducedMotion: false
    // A column's title, at its column's x.
    component ColumnTitle: Label {
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.textSecondary
        font.pointSize: Type.caption
        elide: Text.ElideRight
        Accessible.role: Accessible.ColumnHeader
    }
    // A failure's or a doubt's glyph beside its words: 12 px at 100 %.
    readonly property int markSize: Math.round(Type.bodyPixels * 0.9)

    // ---- WL-9: the last action, held texts, writes, the change mark ----
    // Which of the two kinds of last action shows: a row's write ("write")
    // or what the panel did ("notice"); the newer event wins (D1).
    property string lastEvent: ""
    readonly property string writeLineText: writeRefusalText.length > 0 ? writeRefusalText
        : typeof provider.watchWriteOutcome === "string" ? provider.watchWriteOutcome : ""
    // A new write event: one started, one's outcome, or a refusal to write.
    readonly property string writeEventKey: [typeof provider.watchWriteKind === "string" ? provider.watchWriteKind : "",
                                             typeof provider.watchWriteAt === "number" ? provider.watchWriteAt : 0,
                                             writeRefusalText].join("|")
    onWriteEventKeyChanged: {
        if (writeLineText.length === 0)
            return
        lastEvent = "write"
        const kind = typeof provider.watchWriteKind === "string" ? provider.watchWriteKind : ""
        if (kind === "confirmed")
            flashWriteTime()
        else if (kind === "refused" && writeRefusalText.length === 0)
            announceRefusal()
    }
    Component.onCompleted: if (writeLineText.length > 0) lastEvent = "write"
    // A confirmed write's time flashes once, as Details' (C5).
    property int writeTimeFlashes: 0
    function flashWriteTime() {
        if (reducedMotion)
            return
        ++writeTimeFlashes
        writeTimeFlash.restart()
    }
    // The row last written from here, for what a screen reader hears when
    // the device refuses it.
    property var lastWrite: null
    function writeRow(item, text) {
        // Another write still runs (an editor Tab opened): the text is held,
        // nothing lost, and the line says why.
        if (provider.canWriteWatch !== true) {
            sayNotWritable(item)
            holdRowText(item, text, writeRefusalText.length > 0 ? qsTr("Not written: %1").arg(writeRefusalText)
                                                                 : qsTr("Not written"))
            writeRefusalLeft = true
            return
        }
        lastWrite = { name: item.name, text: text, row: item.index }
        // A text kept by the panel itself goes with a new write of its row.
        if (localHeld[item.name] !== undefined) {
            const next = Object.assign({}, localHeld)
            delete next[item.name]
            localHeld = next
        }
        panel.provider.writeWatch(item.sourceRow, text)
    }
    function announceRefusal() {
        if (!lastWrite)
            return
        const item = watchList.itemAtIndex(lastWrite.row)
        const holds = item && item.name === lastWrite.name && item.ok && item.shownText.length > 0 ? item.shownText : ""
        panel.Accessible.announce(holds.length > 0
            ? qsTr("Write refused: %1 = %2. The device holds %3.").arg(lastWrite.name).arg(lastWrite.text).arg(holds)
            : qsTr("Write refused: %1 = %2.").arg(lastWrite.name).arg(lastWrite.text))
    }
    // Texts held by the panel for a provider that holds none, by path.
    property var localHeld: ({})
    function holdRowText(item, text, reason) {
        if (typeof provider.holdWatchText === "function" && provider.holdWatchText(item.sourceRow, text, reason))
            return
        const next = Object.assign({}, localHeld)
        next[item.name] = { text: text, reason: reason }
        localHeld = next
    }
    // Lets go of what a row holds - a text, or a doubtful write's mark (Esc
    // on the row, its menu); true when there was one.
    function dropRowHeld(item) {
        if (!item)
            return false
        let dropped = false
        if (localHeld[item.name] !== undefined) {
            const next = Object.assign({}, localHeld)
            delete next[item.name]
            localHeld = next
            dropped = true
        }
        if ((item.isHeld || item.differs) && typeof provider.dropWatchHeld === "function")
            dropped = provider.dropWatchHeld(item.sourceRow) || dropped
        return dropped
    }
    // The change mark's clock (E5): one 250 ms timer, running only while a
    // row is marked; rows compare their changedAt with now.
    property double markClock: 0
    property double markUntil: 0
    // A row whose reading changed starts the clock; the row itself reads the
    // time, so nothing it depends on changes while it is worked out.
    function noteChange(at) {
        markUntil = Math.max(markUntil, at + 1000)
        if (Date.now() < markUntil && !markTicker.running)
            markTicker.start()
    }
    Timer {
        id: markTicker
        objectName: "watchMarkTimer"
        interval: 250
        repeat: true
        onTriggered: {
            panel.markClock = Date.now()
            if (panel.markClock >= panel.markUntil + 250)
                stop()
        }
    }
    // The tree's current branch, for its brighter guide (the kinds design, E).
    property int guideLevel: -1
    property int guideFrom: 0
    property int guideTo: -1
    function updateGuides() {
        const span = treeView && watchList.currentIndex >= 0 && typeof rows.branchSpan === "function"
            ? rows.branchSpan(watchList.currentIndex) : null
        guideLevel = span ? span.level : -1
        guideFrom = span ? span.from : 0
        guideTo = span ? span.to : -1
    }
    Timer { id: guideTimer; interval: 0; onTriggered: panel.updateGuides() }
    Connections {
        target: panel.rows
        ignoreUnknownSignals: true
        function onCountChanged() { guideTimer.restart() }
        function onRowsMoved() { guideTimer.restart() }
        function onModelReset() { guideTimer.restart() }
        function onTreeChanged() { guideTimer.restart() }
    }
    // The provider's history (UR-1): where its changes go as steps. Ctrl+Z
    // is the window's UndoRouter's; the notice under the toolbar offers the
    // Undo of what the panel just did, and says what an undo of a Watchlist
    // step did, wherever it was taken.
    readonly property var history: provider && provider.history ? provider.history : null
    Connections {
        target: panel.history
        // A format undone or redone may make the Value column wider.
        function onDone() { panel.forgetMeasured() }
    }
    // Optional host workflow (for example, prepare a project recording before
    // starting). Without handlers the existing provider is called directly.
    property var startHandler: null
    property var stopHandler: null
    property bool operationActive: false
    // Why the row asked to write can't be (a value in a Field larger than one
    // write); shown in the outcome line until another row is chosen.
    property string writeRefusalText: ""
    // The refusal is that changes are off: Allow changes stands beside it.
    property bool writeRefusalChanges: false
    // The refusal says why a row editor left by a click or Tab wrote nothing:
    // it outlives the click's own change of row, until the next edit starts.
    property bool writeRefusalLeft: false
    // Why a row's value can't be written now (WL-5), in words: F2 or a
    // double-click on such a row says it.
    function whyNotWritable(item) {
        const name = item.name
        if (item.model.writeRefusal && item.model.writeRefusal.length > 0)
            return item.model.writeRefusal
        if (item.model.writable !== true)
            return qsTr("%1 is read only.").arg(name)
        if (provider.connected !== true)
            return qsTr("Connect to the device to write %1.").arg(name)
        if (provider.allowMutations === false)
            return qsTr("Changes are off. Allow changes to write %1.").arg(name)
        if (provider.watchWriting === true)
            return qsTr("A write is in progress; write %1 when it is done.").arg(name)
        return ""
    }
    function openNextWritable(from, forward) {
        for (let i = from + (forward ? 1 : -1); i >= 0 && i < watchList.count; i += forward ? 1 : -1) {
            watchList.positionViewAtIndex(i, ListView.Contain)
            watchList.forceLayout()
            const item = watchList.itemAtIndex(i)
            // Tab goes on to the next editable value even while the write it
            // made still runs; that value's own commit waits its turn (C2).
            if (item && item.isValue && item.editableRow) {
                item.startEdit(true)
                return true
            }
        }
        watchList.forceActiveFocus()
        return false
    }
    function sayNotWritable(item) {
        writeRefusalText = whyNotWritable(item)
        writeRefusalChanges = item.model.writable === true && provider.connected === true && provider.allowMutations === false
    }
    function requestStart(polling) {
        if (typeof startHandler === "function") startHandler(polling)
        else if (polling) provider.startWatch()
        else provider.refreshWatch()
    }
    function requestStop() {
        if (typeof stopHandler === "function") stopHandler()
        else provider.stopWatch()
    }
    objectName: "watchlistPane"
    palette.window: Theme.surfaceWindow
    palette.windowText: Theme.textPrimary
    palette.base: Theme.surface
    palette.alternateBase: Theme.surfaceAlternate
    palette.text: Theme.textPrimary
    palette.button: Theme.buttonFill
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.stateSelected
    palette.highlightedText: Theme.selectedForeground
    palette.placeholderText: Theme.textMuted
    palette.disabled.buttonText: Theme.textDisabled
    palette.disabled.windowText: Theme.textDisabled
    palette.disabled.text: Theme.textDisabled
    property bool expanded: false
    // A host that gives the Watchlist its own dock sets this false: the list is
    // then always open and its title, which folds it, is not shown.
    property bool collapsible: true
    // The rows show when the list is expanded, or always when it cannot fold.
    readonly property bool open: expanded || !collapsible
    // `framed: false` drops the card for a dock.
    property bool framed: true
    // The polling status as a line under the toolbar. A host whose status bar
    // already says it sets this false; advice it would cut short still shows.
    property bool statusShown: true
    // The device cannot read several Fields at once and the Watchlist still
    // asks it to: the status is advice, read whole, in the warning colour.
    readonly property bool readManyAdvice: provider.watchReadManySupported === false
                                           && provider.watchRequiresExplicitIndividual === true
                                           && provider.watchIndividual !== true

    readonly property bool active: provider.watchActive === true || operationActive
    // Rows are added, removed and cleared while polling when the provider
    // takes changes between two reads; the interval and modes still wait.
    readonly property bool changeLocked: provider.watchActive === true && provider.watchChangesWhilePolling !== true
    readonly property int count: provider.watchCount
    readonly property bool connected: provider.connected === true
    // Some row is on this device (WL-4c); a provider that does not say is.
    readonly property bool readable: provider.watchReadable !== false
    readonly property bool canRead: connected && count > 0 && readable && !active
    readonly property string statusText: paceText.length > 0 ? paceText
                                       : provider.watchPending ? qsTr("Reading…")
                                       : provider.watchStatus ? provider.watchStatus : ""

    // How polling keeps pace with its interval (WL-6), from the provider's
    // watchPace: a line in place of the status from the first completed cycle
    // on, shown even where a host hides the status, since it corrects the
    // interval the toolbar shows; its tooltip; and what a screen reader hears
    // when polling falls behind or catches up. A wait, a failure or a stop
    // still says itself.
    readonly property var pace: provider.watchPace !== undefined && provider.watchPace !== null ? provider.watchPace : ({})
    readonly property bool paceKnown: typeof pace.cycles === "number" && pace.cycles > 0
    readonly property bool pacePolling: paceKnown && pace.polling === true && provider.watchActive === true
    readonly property bool paceBehind: pacePolling && pace.behind === true
    // Continuous, yet slower than its own cycles: other requests went first.
    readonly property bool paceCrowded: pacePolling && pace.intervalMs === 0 &&
                                        pace.achievedPeriodMs >= 1.25 * pace.medianCycleMs
    readonly property string paceLine: {
        if (!paceKnown || pace.routine !== true)
            return ""
        const one = pace.values === 1
        const took = durationText(pace.polling ? pace.medianCycleMs : pace.lastCycleMs)
        if (!pace.polling)
            return provider.watchActive === true ? "" : one ? qsTr("1 value read in %1").arg(took)
                                                            : qsTr("%L1 values read in %2").arg(pace.values).arg(took)
        if (!pacePolling)
            return ""
        if (paceBehind) {
            const every = intervalText(pace.achievedPeriodMs), asked = intervalText(pace.intervalMs)
            return one ? qsTr("Every %1, not %2: a cycle of 1 value takes %3").arg(every).arg(asked).arg(took)
                       : qsTr("Every %1, not %2: a cycle of %L3 values takes %4").arg(every).arg(asked).arg(pace.values).arg(took)
        }
        if (paceCrowded) {
            const every = durationText(pace.achievedPeriodMs)
            return one ? qsTr("Every %1: a cycle of 1 value takes %2").arg(every).arg(took)
                       : qsTr("Every %1: a cycle of %L2 values takes %3").arg(every).arg(pace.values).arg(took)
        }
        return one ? qsTr("A cycle of 1 value takes %1").arg(took)
                   : qsTr("A cycle of %L1 values takes %2").arg(pace.values).arg(took)
    }
    readonly property string paceTip: {
        if (!paceKnown)
            return ""
        const lines = []
        if (pace.polling && pace.intervalMs === 0)
            lines.push(qsTr("Continuous: each cycle starts when the last one ends, after other requests."))
        else if (paceBehind)
            lines.push(qsTr("Every %1, not %2: a cycle due while the last one still runs is skipped.")
                       .arg(intervalText(pace.achievedPeriodMs)).arg(intervalText(pace.intervalMs)))
        else if (pace.polling)
            lines.push(qsTr("Every %1, as asked.").arg(intervalText(pace.intervalMs)))
        const kinds = []
        if (pace.readMany > 0) kinds.push(qsTr("%L1 ReadMany").arg(pace.readMany))
        if (pace.batch > 0) kinds.push(qsTr("%L1 Batch").arg(pace.batch))
        if (pace.fieldRead > 0) kinds.push(qsTr("%L1 FieldRead").arg(pace.fieldRead))
        const exchanges = pace.readMany + pace.batch + pace.fieldRead
        const joined = kinds.length < 2 ? kinds.join("")
                     : kinds.slice(0, -1).join(", ") + qsTr(" and ") + kinds[kinds.length - 1]
        const requests = exchanges === 1 ? qsTr("%1 request").arg(joined) : qsTr("%1 requests").arg(joined)
        lines.push(pace.values === 1 ? qsTr("1 value in %1; the last cycle took %2.").arg(requests).arg(durationText(pace.lastCycleMs))
                                     : qsTr("%L1 values in %2; the last cycle took %3.").arg(pace.values).arg(requests)
                                         .arg(durationText(pace.lastCycleMs)))
        if (pace.again > 0)
            lines.push(qsTr("Written Fields read again: %L1.").arg(pace.again))
        if (paceBehind)
            lines.push(qsTr("Continuous reads as soon as a cycle ends; fewer values make a cycle shorter. Stop to change the interval."))
        return lines.join("\n")
    }
    // A time in words: milliseconds to two significant digits below a
    // second; one decimal up to ten (more only where one would read as the
    // interval); whole seconds up to a minute; then minutes and seconds.
    function durationText(ms) {
        if (!(ms >= 0))
            return ""
        if (ms < 1000)
            return qsTr("%L1 ms").arg(ms < 10 ? Math.round(ms) : Number(ms.toPrecision(2)))
        if (ms < 10000) {
            let seconds = Number((ms / 1000).toFixed(1))
            if (seconds * 1000 === provider.watchIntervalMs && Math.abs(ms - provider.watchIntervalMs) >= 0.5)
                seconds = Number((ms / 1000).toFixed(3))
            return qsTr("%L1 s").arg(seconds)
        }
        if (ms < 60000)
            return qsTr("%L1 s").arg(Math.round(ms / 1000))
        const whole = Math.round(ms / 1000)
        return qsTr("%1 min %2 s").arg(Math.floor(whole / 60)).arg(whole % 60)
    }
    // The line changes at most every two seconds, so a cycle's figure does not
    // flicker; it goes at once when there is nothing to say.
    property string paceText: ""
    property double paceTaken: 0
    onPaceLineChanged: {
        const since = Date.now() - paceTaken
        if (paceLine.length === 0 || paceText.length === 0 || since >= 2000) {
            paceSettle.stop()
            paceText = paceLine
            paceTaken = Date.now()
        } else if (!paceSettle.running) {
            paceSettle.interval = Math.max(1, 2000 - since)
            paceSettle.start()
        }
    }
    Timer {
        id: paceSettle
        onTriggered: {
            panel.paceText = panel.paceLine
            panel.paceTaken = Date.now()
        }
    }
    // Falling behind and catching up are said, at most every 30 s.
    property double paceSaid: 0
    onPaceBehindChanged: {
        if (Date.now() - paceSaid < 30000 || !pacePolling)
            return
        paceSaid = Date.now()
        panel.Accessible.announce(paceBehind
            ? qsTr("Reading every %1, not every %2: a cycle takes %3").arg(intervalText(pace.achievedPeriodMs))
                  .arg(intervalText(pace.intervalMs)).arg(durationText(pace.medianCycleMs))
            : qsTr("Reading every %1 again").arg(intervalText(pace.intervalMs)))
    }
    // With watch boxes in the Device tree a Field is watched by ticking it;
    // without them, by "Watch the selected Field".
    readonly property bool treeBoxes: typeof provider.watchState === "function" &&
                                      typeof provider.setWatchMany === "function"
    // The "+" row, where a provider resolves typed paths (WL-4b).
    readonly property bool addAvailable: typeof provider.addWatchPaths === "function" &&
                                         typeof provider.watchPathCandidates === "function" &&
                                         typeof provider.checkWatchPath === "function"

    // The rows as shown, from the provider: one per value with its whole
    // path, or grouped by path - group, Field, member - folding as the Device
    // tree does (a WatchRows). The view's only: polling, writes and recording
    // are the same in both. A provider without it shows its watchlist as a
    // list. The provider keeps the rows, so a fold outlives a closed list.
    readonly property var rows: provider.watchRows ?? null
    readonly property bool treeAvailable: rows !== null
    readonly property bool treeView: rows !== null && rows.tree === true
    // How the numbers are shown (WL-3): the Watchlist's form, and a row's
    // own. The view's only; a write takes any form whatever is shown.
    readonly property bool formatAvailable: typeof provider.watchFormat === "string"
    readonly property string format: formatAvailable ? provider.watchFormat : "original"
    // Each form changes something (WL-9 B1); the same words are the
    // segments' accessible descriptions, so the difference is not hover-only.
    readonly property var formats: [
        { name: "original", text: qsTr("Original"), tip: qsTr("Original: as the type means it (0.1, Idle (2), false (0))") },
        { name: "dec", text: qsTr("Dec"), tip: qsTr("Dec: every digit a float holds (0.1 shows as 0.100000001); integers and codes as Original") },
        { name: "hex", text: qsTr("Hex"), tip: qsTr("Hex: the bits (0x3DCCCCCD, Idle (0x02))") }
    ]
    function formatWord(name) {
        const found = formats.find(item => item.name === name)
        return found ? found.text : name
    }
    function setFormat(name) {
        if (!formatAvailable || name === format)
            return
        if (typeof provider.chooseWatchFormat === "function")
            provider.chooseWatchFormat(name)
        else
            provider.watchFormat = name
    }
    // Every value back to Original at once (WL-3b): the Watchlist's form and
    // every row's own, those kept for paths not watched now too. One change,
    // which Undo takes back while no form has been chosen since.
    readonly property bool formatsResettable: formatAvailable && typeof provider.resetWatchFormats === "function"
                                              && typeof provider.watchFormatsDiffer === "function"
    // Whether a value is shown other than as Original; a method of the
    // provider's, so menus ask it as they open.
    function formatsDiffer() {
        return formatsResettable && provider.watchFormatsDiffer() === true
    }
    // Where a plain item's label starts in a menu of checkable ones, so the
    // labels line up: a style that draws the check inside the left padding
    // has kept the column there (TelemetryDark), one that draws it at the
    // padding's edge pushes the label past it (Basic).
    function checkColumn(checkable, fallback) {
        if (!checkable)
            return fallback
        return checkable.leftPadding + (checkable.indicator && checkable.indicator.x >= checkable.leftPadding
                                        ? checkable.indicator.width + checkable.spacing : 0)
    }
    function resetFormats() {
        if (!formatsResettable)
            return false
        const result = provider.resetWatchFormats()
        if (!result || result.status !== "ok")
            return false
        forgetMeasured()
        say(result.rows === 1 ? qsTr("Number formats reset to Original; 1 value had its own")
            : result.rows > 1 ? qsTr("Number formats reset to Original; %Ln values had their own", "", result.rows)
            : qsTr("Number formats reset to Original"), typeof result.serial === "number" ? result.serial : 0)
        return true
    }
    // The Value column grows only, until a form is chosen: hex is wider.
    onFormatChanged: forgetMeasured()
    // Gives what row shows - a value, or every value under a node - a format
    // of its own, or "" to follow the Watchlist's.
    function formatRows(row, name) {
        if (typeof provider.setWatchRowFormat !== "function" || row < 0)
            return
        const many = onSelection(row)
        const sources = sourcesAt(row)
        if (sources.length === 0)
            return
        let set = false
        const serial = notice.recording(() => { set = provider.setWatchRowFormat(sources, name) })
        if (!set)
            return
        forgetMeasured()
        if (many)
            say(name.length === 0 ? qsTr("%Ln values shown like the Watchlist (%1)", "", sources.length).arg(formatWord(format))
                                  : qsTr("%Ln values shown in %1", "", sources.length).arg(formatWord(name)),
                serial)
    }
    // The selection (WL-2c), kept by the rows by path; the current row is
    // the keyboard's cursor. An action takes the selection when the current
    // row is in a selection of two or more, else the row it is asked for.
    readonly property bool selectable: rows !== null && typeof rows.select === "function"
    readonly property int selectedCount: selectable ? rows.selectedCount : 0
    function onSelection(row) {
        return selectable && selectedCount >= 2 && rows.isSelected(row)
    }
    // The source rows an action at row takes, each once, in the order shown.
    function sourcesAt(row) {
        return onSelection(row) ? rows.selectedSourceRows() : rows ? rows.sourceRows(row) : [row]
    }
    // Makes row current: "select" selects it alone, "extend" from the
    // anchor (Shift), "cursor" moves the cursor only (Ctrl).
    function moveTo(row, how) {
        if (row < 0 || row >= watchList.count)
            return
        const from = watchList.currentIndex
        watchList.currentIndex = row
        if (selectable && how === "extend") {
            if (rows.anchorRow() < 0 && from >= 0 && from !== row)
                rows.select(from)
            rows.selectRange(row)
        } else if (selectable && how === "select") {
            rows.select(row)
        }
        selectionGesture()
        watchList.positionViewAtIndex(row, ListView.Contain)
    }
    // The row a menu was opened for.
    property int menuRow: -1
    function openRowMenu(row, item, x, y) {
        menuRow = row
        rowMenu.popup(item, x, y)
    }
    // How many are selected, said once the selection settles; one row moved
    // to is the row's own name, not a count.
    property int announcedCount: 0
    Timer {
        id: selectionAnnounce
        interval: 300
        onTriggered: {
            const n = panel.selectedCount
            const values = n > 1 ? panel.rows.selectedSourceRows().length : 0
            if (!watchList.activeFocus) {
                panel.announcedCount = n
                return
            }
            if (n > 1)
                panel.Accessible.announce(qsTr("%Ln values selected", "", values))
            else if (n === 0 && panel.announcedCount > 1)
                panel.Accessible.announce(qsTr("Selection cleared"))
            panel.announcedCount = n
        }
    }
    // The user's own selection gestures, said once they settle; a change
    // the panel or the provider makes (a removal, a fold) is not said.
    function selectionGesture() {
        if (selectable && watchList.activeFocus)
            selectionAnnounce.restart()
    }
    // What a sort did, said after the menu that asked for it has closed.
    property string sortSaid: ""
    Timer {
        id: sortAnnounce
        interval: 300
        onTriggered: panel.Accessible.announce(panel.sortSaid)
    }
    // The list by name A-Z, or as added; the rows move, the cursor's row
    // stays current and in view.
    readonly property bool sortable: rows !== null && typeof rows.sorted === "boolean"
    readonly property bool sorted: sortable && rows.sorted
    function setSorted(on) {
        if (!sortable || rows.sorted === on)
            return
        rows.sorted = on
        // The view takes the moves at its next layout: now, so the cursor's
        // row is read where it went.
        watchList.forceLayout()
        if (watchList.currentIndex >= 0)
            watchList.positionViewAtIndex(watchList.currentIndex, ListView.Contain)
        sortSaid = on ? qsTr("Sorted by name, A to Z") : qsTr("Back to the order added")
        sortAnnounce.restart()
    }
    // Switches the view; the current value stays current, its parents opened.
    function showTree(tree) {
        if (!rows || tree === treeView)
            return
        const current = watchList.currentIndex
        const sources = current >= 0 ? rows.sourceRows(current) : []
        rows.tree = tree
        const row = sources.length > 0 ? rows.reveal(sources[0]) : -1
        watchList.currentIndex = row
        if (row >= 0)
            watchList.positionViewAtIndex(row, ListView.Contain)
    }
    // The last thing done, in a line under the toolbar: "Removed
    // device.Config.Gain · Undo" (an UndoNotice). Like the Device tree's, it
    // goes once anything changes the Watchlist, the tree or a format - here,
    // in the Device tree, or by a project loaded. Its Undo shows while the
    // step it speaks of is the one Undo would take.
    readonly property int revision: typeof provider.watchRevision === "number" ? provider.watchRevision : -1
    function say(text, serial) {
        notice.say(text, serial)
        if (text.length > 0)
            lastEvent = "notice"
        // The notice is what is said, not the selection it left.
        selectionAnnounce.stop()
        announcedCount = selectedCount
    }
    // Stops watching what row shows: a value, or every value under a node, as
    // one change where the provider takes several, said with Undo. Never
    // asked first: Undo brings the rows back, device or not.
    function removeAt(row, own) {
        const many = own !== true && onSelection(row)
        const sources = many ? sourcesAt(row) : rows ? rows.sourceRows(row) : [row]
        if (sources.length === 0 || changeLocked)
            return
        // A selection is named by its count: its rows may be out of view.
        const item = many ? null : watchList.itemAtIndex(row)
        const label = item ? item.name : ""
        if (typeof provider.removeWatchRows !== "function") {
            sources.slice().sort((a, b) => b - a).forEach(source => provider.removeWatch(source))
            selectCurrent()
            return
        }
        const result = provider.removeWatchRows(sources)
        if (!result || result.status !== "ok" || result.changed === 0)
            return
        selectCurrent()
        say(many ? qsTr("Removed %Ln values", "", result.changed)
            : sources.length === 1 || label.length === 0
            ? (label.length > 0 ? qsTr("Removed %1").arg(label) : qsTr("Removed %L1").arg(result.changed))
            : qsTr("Removed %L1 values of %2").arg(result.changed).arg(label),
            typeof result.serial === "number" ? result.serial : 0)
    }
    // Clear the Watchlist: every value, one step, never asked first.
    function clearAll() {
        if (count === 0 || changeLocked)
            return
        const cleared = count
        const serial = notice.recording(() => provider.clearWatch())
        if (count === 0)
            say(qsTr("Cleared %Ln values", "", cleared), serial)
    }
    // After a removal the row the cursor moved to is the one selected.
    function selectCurrent() {
        if (selectable && watchList.currentIndex >= 0)
            rows.select(watchList.currentIndex)
        announcedCount = selectedCount
    }
    // Copies what row shows: "path = value" lines (Ctrl+C), the paths
    // alone (Alt+Shift+C), or the value alone.
    function copyRows(row, what) {
        if (row < 0)
            return false
        const item = watchList.itemAtIndex(row)
        let text = ""
        if (what === "value")
            text = item && item.isValue ? item.shownText : ""
        else if (onSelection(row))
            text = rows.selectedLines(what === "paths")
        else if (rows)
            text = rows.lines(row, what === "paths")
        else if (item)
            text = what === "paths" || item.shownText.length === 0 ? item.name : item.name + " = " + item.shownText
        if (text.length === 0)
            return false
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.text = ""
        const lines = text.split("\n").length
        say(what === "value" ? qsTr("Copied the value of %1").arg(item.name)
            : lines === 1 ? qsTr("Copied %1").arg(what === "paths" ? text : item ? item.name : text)
            : what === "paths" ? qsTr("Copied %L1 paths").arg(lines)
            : qsTr("Copied %L1 values").arg(lines), 0)
        return true
    }

    // What a row's drag carries (LP-1g3): its values' paths - every value under
    // a node, the selection's when the row is in it - for the Plot, the Batch
    // or another Watchlist.
    function dragPaths(row) {
        if (!rows || row < 0)
            return []
        const text = onSelection(row) ? rows.selectedLines(true) : rows.lines(row, true)
        return text.length > 0 ? text.split("\n") : []
    }
    // A drop of paths from the Device tree adds them, as the "+" row would.
    // A drop of rows picked in the Device tree (section 16): every value
    // under them, at any depth, as one step; Commands and Services left out
    // and said. The added rows become the selection in the List; a dropped
    // group becomes current in the Tree, open one level, in view. When
    // everything is watched already, the drop selects those rows.
    function dropItems(items) {
        if (!Array.isArray(items) || items.length === 0 || typeof provider.dropOn !== "function")
            return false
        const from = count
        const result = provider.dropOn(items, "watchlist")
        const added = typeof result.added === "number" ? result.added : 0
        say(result.message || result.refusal || "", added > 0 && typeof result.serial === "number" ? result.serial : 0)
        if (!rows)
            return added > 0
        if (result.status === "held" && Array.isArray(result.rows) && result.rows.length > 0) {
            const shown = result.rows.map(source => rows.reveal(source)).filter(row => row >= 0)
            if (shown.length > 0)
                selectShown(shown)
            return false
        }
        if (added <= 0)
            return false
        if (treeView && items.length === 1 && items[0].kind === "group") {
            const row = rows.revealPath(items[0].path)
            if (row >= 0) {
                rows.setExpanded(row, true)
                watchList.currentIndex = row
                if (selectable)
                    rows.select(row)
                watchList.positionViewAtIndex(row, ListView.Contain)
            }
            return true
        }
        const shown = []
        for (let source = from; source < count; ++source) {
            const row = rows.reveal(source)
            if (row >= 0)
                shown.push(row)
        }
        selectShown(shown)
        return true
    }
    // The rows given become the selection, the first current and in view.
    function selectShown(shown) {
        if (shown.length === 0)
            return
        shown.sort((a, b) => a - b)
        watchList.currentIndex = shown[0]
        if (selectable) {
            rows.select(shown[0])
            for (let i = 1; i < shown.length; ++i)
                rows.select(shown[i], true)
        }
        watchList.positionViewAtIndex(shown[0], ListView.Contain)
    }
    function dropPaths(payload) {
        if (!payload || payload.from === "watchlist")
            return false
        if (Array.isArray(payload.items) && typeof provider.dropOn === "function")
            return dropItems(payload.items)
        if (typeof provider.addWatchPaths !== "function")
            return false
        const result = provider.addWatchPaths(payload.paths || [])
        const added = result && typeof result.added === "number" ? result.added : 0
        say(added === 1 ? qsTr("Added 1 value to the Watchlist") : qsTr("Added %L1 values to the Watchlist").arg(added),
            added > 0 && typeof result.serial === "number" ? result.serial : 0)
        return added > 0
    }
    // Row's values - the selection's, when row is in it - into the Batch
    // (section 16.4, Ctrl+B): one item per value, writing it, as a drop of
    // them does; all or none, one step, said on the line.
    readonly property bool batchAvailable: !!provider && typeof provider.dropOn === "function"
    function batchRows(row) {
        if (!batchAvailable || !rows || row < 0)
            return false
        const text = onSelection(row) ? rows.selectedLines(true) : rows.lines(row, true)
        const items = text.split("\n").filter(path => path.length > 0).map(path => {
            const leaf = path.substring(path.lastIndexOf("/") + 1)
            return { kind: /[.\[]/.test(leaf) ? "member" : "field", path: path }
        })
        if (items.length === 0)
            return false
        const result = provider.dropOn(items, "batch")
        const added = result.status === "ok" && result.added > 0
        say(result.message || result.refusal || "", added && typeof result.serial === "number" ? result.serial : 0)
        return added
    }
    // The live Plot (LP-1), when the provider has one.
    readonly property var plotTraces: provider && provider.watchTraces ? provider.watchTraces : null
    // Puts row's values - the selection's, when row is in it, or every value
    // under a node - on the Plot, or takes them off when each is on it
    // already (Ctrl+P, the row menu), as one step. Text is refused in words.
    function togglePlot(row) {
        if (!plotTraces || !rows || row < 0)
            return false
        const values = rows.plotValues(row)
        if (values.length === 0)
            return false
        const numbers = values.filter(value => value.number)
        const paths = numbers.map(value => value.path)
        if (numbers.length > 0 && numbers.every(value => plotTraces.isPlotted(value.path))) {
            const off = notice.recording(() => plotTraces.unplotMany(paths))
            say(numbers.length === 1 ? qsTr("%1 is off the Plot; its readings are kept").arg(numbers[0].path)
                                     : qsTr("%L1 values are off the Plot; their readings are kept").arg(numbers.length),
                off)
            return true
        }
        let plotted = null
        const recorded = notice.recording(() => { plotted = plotTraces.plotMany(paths) })
        const added = plotted.added
        const last = plotted.last
        const refusal = plotted.refusal
        const serial = added > 0 ? recorded : 0
        const text = values.length - numbers.length
        if (refusal.length > 0)
            say(refusal, 0)
        else if (text > 0 && values.length === 1)
            say(qsTr("%1 is text; only numbers, booleans and enums are plotted.").arg(values[0].path), 0)
        else if (text > 0)
            say(qsTr("%L1 of %L2 values are text and were not plotted.").arg(text).arg(values.length), 0)
        else
            say(added === 1 ? qsTr("Plotted %1").arg(last)
                            : qsTr("Plotted %L1 values").arg(added), serial)
        return true
    }

    // A row's status in a word or two; the provider's own word is the tooltip.
    // A word not known here is shown as it is.
    function statusWord(status) {
        switch (status) {
        case "Ok": return qsTr("OK")
        case "Pending": return qsTr("Reading…")
        // A row added while polling waits for the next read; that is not a problem.
        case "Not read": case "NotRead": return panel.provider.watchActive === true ? qsTr("Next read") : qsTr("Not read")
        case "Stale": return qsTr("Stale")
        // A path this device does not have: a fact, not a failure (WL-4c).
        case "Missing": return qsTr("Not on device")
        case "Unwatchable": return qsTr("Can't watch")
        case "NotFound": return qsTr("Not found")
        case "Unsupported": case "UnsupportedType": return qsTr("Unsupported")
        case "Unavailable": return qsTr("Unavailable")
        case "ReadError": return qsTr("Read error")
        case "MalformedReply": return qsTr("Bad reply")
        }
        if (status.startsWith("SubmissionRefused"))
            return qsTr("Refused")
        if (/time(d)?\s*out/i.test(status))
            return qsTr("Timed out")
        return status
    }

    // What gives way first as the panel narrows: "Every", then Read once,
    // then List | Tree. Measured from the controls, so it holds at any zoom;
    // the toolbar's controls are as wide as their text, whatever the style's
    // minimum, so the row holds the same in any host.
    readonly property real barGap: Type.spacing
    readonly property real barBase: (fold.visible ? fold.implicitWidth + barGap : 0)
                                    + start.barWidth + barGap + interval.barWidth
                                    + 2 * barGap + more.implicitWidth
    // The mark of values in a form of their own is always in the row where it
    // can show, clear when it counts nothing, so it moves nothing.
    readonly property bool ownMarkShown: formatsResettable && typeof provider.watchOwnFormatCount === "number"
    readonly property int ownFormatRows: ownMarkShown ? provider.watchOwnFormatCount : 0
    readonly property real ownMarkWidth: ownMarkShown ? ownFormats.implicitWidth + 2 * barGap : 0
    readonly property real formatButtonWidth: formatAvailable ? formatButton.implicitWidth + 2 * barGap + ownMarkWidth : 0
    readonly property real formatSegmentsWidth: formatAvailable ? formatSegments.implicitWidth + 2 * barGap + ownMarkWidth : 0
    // The bar's room: from L to R (WL-9 E1).
    readonly property real barRoom: Math.max(0, availableWidth - Type.padding - scrollReserve)
    readonly property bool viewInBar: treeAvailable
                                      && barRoom >= barBase + formatButtonWidth + viewSegments.implicitWidth + barGap
    readonly property real barCore: barBase + (viewInBar ? viewSegments.implicitWidth + barGap : 0) + formatButtonWidth
    readonly property bool readOnceInBar: (viewInBar || !treeAvailable)
                                          && barRoom >= barCore + readOnce.barWidth + barGap
    readonly property bool formatInBar: formatAvailable && readOnceInBar
        && barRoom >= barCore - formatButtonWidth + formatSegmentsWidth + readOnce.barWidth + barGap
    // Every control of the bar at its widest, "Every" included.
    readonly property real barFull: barCore - (formatAvailable ? formatButtonWidth - formatSegmentsWidth : 0)
                                    + readOnce.barWidth + every.implicitWidth + 2 * barGap
    readonly property bool everyShown: readOnceInBar && (formatInBar || !formatAvailable) && barRoom >= barFull
    // The pace beside the interval it qualifies (WL-9 D1), while the bar
    // holds it whole beside every other control; it is the first to leave.
    readonly property bool statusLineShown: (statusShown || readManyAdvice || paceText.length > 0) && statusText.length > 0
    readonly property bool statusWarns: readManyAdvice || (paceBehind && paceText.length > 0 && statusText === paceText)
    TextMetrics { id: paceMetrics; font.family: panel.font.family; font.pointSize: Type.caption; text: panel.statusText }
    readonly property bool paceInBar: everyShown && statusLineShown && paceText.length > 0 && statusText === paceText
        && barRoom >= barFull + Type.groupSpacing + Math.ceil(paceMetrics.advanceWidth)
                      + (statusWarns ? markSize + Type.spacing : 0)
    // The row family's columns (WL-9 A1-A2): Name, Value, Type, rw, Status
    // and ×, one columnGap apart. Value, Type and Status are as wide as the
    // widest text they have shown, growing only until the list is cleared or
    // the zoom changes, so a reading never makes them jump; rw is two
    // letters. Every column stands at one x on every row (valueX, typeX,
    // accessX, statusX, removeX). The Name column fills up to its cap: the
    // widest name's end plus a gap, or 24 body heights, so in a wide dock the
    // values stay beside their names and the spare room lies after ×. As the
    // room for names shrinks (on the deepest row shown): rw hides first, then
    // Status becomes a glyph whose word is in its tooltip, then names elide
    // down to their floor, then a row's type gives way at its right. Value
    // never narrows below its widest number, Type never hides, nothing
    // scrolls sideways.
    readonly property real columnGap: Type.columnGap
    readonly property real removeColumnWidth: Type.rowHeight
    // R: the rows' right line, clear of the scroll bar.
    readonly property real listWidth: Math.max(0, watchList.width - scrollReserve)
    property real valueSeen: 0
    property real typeSeen: 0
    // The widest type with its scopes cut to "…::" (the review's 14.2).
    property real typeShortSeen: 0
    property real typeShortPending: 0
    property real statusSeen: 0
    property bool accessSeen: false
    property real nameSeen: 0
    property int deepestSeen: 0
    property real valuePending: 0
    property real typePending: 0
    property real statusPending: 0
    property bool accessPending: false
    property real namePending: 0
    property int deepestPending: 0
    function noteNumber(width) {
        if (width > numberSeen)
            numberSeen = width
    }
    function noteShortType(width) {
        if (width > typeShortPending) {
            typeShortPending = width
            applyMeasured.restart()
        }
    }
    function noteMeasured(value, type, status, access) {
        if (value > valuePending || type > typePending || status > statusPending || (access && !accessPending)) {
            valuePending = Math.max(valuePending, value)
            typePending = Math.max(typePending, type)
            statusPending = Math.max(statusPending, status)
            accessPending = accessPending || access
            applyMeasured.restart()
        }
    }
    // A name's right end from the row's left (indent, chevron and glyph
    // included), and its row's depth: the cap and the give-way follow them.
    function noteName(end, depth) {
        if (end > namePending || depth > deepestPending) {
            namePending = Math.max(namePending, end)
            deepestPending = Math.max(deepestPending, depth)
            applyMeasured.restart()
        }
    }
    // Forgets the widths (a cleared list, a new zoom) and asks every row to
    // measure itself again: a type's text never changes, so its row would
    // not report it a second time by itself.
    property int measureEpoch: 0
    function forgetMeasured() {
        valuePending = typePending = statusPending = namePending = typeShortPending = 0
        deepestPending = 0
        accessPending = false
        valueSeen = typeSeen = statusSeen = numberSeen = nameSeen = typeShortSeen = 0
        deepestSeen = 0
        accessSeen = false
        ++measureEpoch
    }
    // Rows report while they are made; the widths change once, after.
    Timer {
        id: applyMeasured
        interval: 0
        onTriggered: {
            panel.valueSeen = Math.max(panel.valueSeen, panel.valuePending)
            panel.typeSeen = Math.max(panel.typeSeen, panel.typePending)
            panel.typeShortSeen = Math.max(panel.typeShortSeen, panel.typeShortPending)
            panel.statusSeen = Math.max(panel.statusSeen, panel.statusPending)
            panel.accessSeen = panel.accessSeen || panel.accessPending
            panel.nameSeen = Math.max(panel.nameSeen, panel.namePending)
            panel.deepestSeen = Math.max(panel.deepestSeen, panel.deepestPending)
        }
    }
    onCountChanged: if (count === 0) forgetMeasured()
    onTreeViewChanged: forgetMeasured()
    Connections {
        target: Type
        function onBodyPixelsChanged() { panel.forgetMeasured() }
    }
    // Numbers never elide: the column is as wide as the widest one, up to
    // more than half of a narrow list.
    // Room for the pencil at the value's left while rows can be written, so
    // it never sits on the digits.
    readonly property real pencilRoom: 0
    property real numberSeen: 0
    readonly property real valueColumn: count === 0 ? 0
        : Math.max(Math.round(Type.bodyPixels * 4.2),
                   Math.min(Math.ceil(numberSeen) + pencilRoom, listWidth * 0.55),
                   Math.min(Math.ceil(valueSeen) + pencilRoom, Type.bodyPixels * 16.5, listWidth * 0.4))
    // Type never hides (WL-9 A2): as wide as the widest type, 3 to 9.6 body
    // heights; a row whose name would drop below its floor takes its room
    // from its own type, which elides at its right, the whole in its tooltip.
    readonly property real typeWideColumn: typeSeen > 0 ? Math.max(Type.bodyPixels * 3, Math.min(Math.ceil(typeSeen), Type.bodyPixels * 9.6)) : 0
    readonly property real typeNarrowColumn: typeShortSeen > 0
        ? Math.max(Type.bodyPixels * 3, Math.min(Math.ceil(typeShortSeen), typeWideColumn)) : typeWideColumn
    // The type scopes give way third (the review's 14.2, after rw and
    // Status, before any name elides): the whole column shows "…::Config"
    // while the deepest name would keep less than 7.2 body heights.
    readonly property bool typeScopesCut: typeNarrowColumn < typeWideColumn
        && deepestRoom(accessColumn, statusColumn, typeWideColumn) < Type.bodyPixels * 7.2
    readonly property real typeColumn: typeScopesCut ? typeNarrowColumn : typeWideColumn
    readonly property real typeLeast: Math.round(Type.bodyPixels * 1.5)
    readonly property real accessFull: accessSeen ? Math.round(Type.bodyPixels * 1.05) : 0
    TextMetrics { id: statusTitleMetrics; font.family: panel.font.family; font.pointSize: Type.caption; text: qsTr("Status") }
    readonly property real statusFull: statusSeen > 0 ? Math.ceil(Math.max(statusSeen, statusTitleMetrics.advanceWidth)) : 0
    readonly property real glyphWidth: Math.round(Type.bodyPixels * 1.05)
    // Where a row's name starts: in the tree after its chevron and its kind
    // glyph (42 + 16 × depth at 100 %), in the list on L.
    function nameXAt(depth) {
        return treeView ? Type.padding + (depth + 1) * Type.indent + Type.iconSize + Type.spacing : Type.padding
    }
    // Where Value starts when the Name column fills: everything after it is
    // as wide as it is, × ending on R.
    function fillValueX(access, status, type) {
        return listWidth - removeColumnWidth - columnGap
            - (status > 0 ? status + columnGap : 0) - (access > 0 ? access + columnGap : 0)
            - (type > 0 ? type + columnGap : 0) - valueColumn
    }
    // The room for names on the deepest row shown, with these columns.
    function deepestRoom(access, status, type) {
        return fillValueX(access, status, type) - columnGap - nameXAt(treeView ? deepestSeen : 0)
    }
    readonly property real accessColumn: accessFull > 0 && deepestRoom(accessFull, statusFull, typeWideColumn) >= Type.bodyPixels * 7.2
                                         ? accessFull : 0
    readonly property bool statusGlyph: statusFull > 0 && deepestRoom(accessColumn, statusFull, typeWideColumn) < Type.bodyPixels * 7.2
    readonly property real statusColumn: statusFull <= 0 ? 0 : statusGlyph ? glyphWidth : statusFull
    // The Name column's cap: the widest name and a gap, at least 24 body
    // heights, and a ceiling of 40 (about 75 characters, §1.2's line length):
    // only a path past the ceiling elides, by whole segments, so one long
    // path never puts every value far from its name (the review's 14.3,
    // which amends WL-9's "the cap never elides a name").
    readonly property real nameCap: Math.max(Type.bodyPixels * 24, Math.min(nameSeen + columnGap, Type.bodyPixels * 40))
    readonly property real valueX: Math.max(nameXAt(0), Math.min(fillValueX(accessColumn, statusColumn, typeColumn), nameCap + columnGap))
    readonly property real typeX: valueX + valueColumn + columnGap
    readonly property real accessX: typeX + (typeColumn > 0 ? typeColumn + columnGap : 0)
    readonly property real statusX: accessX + (accessColumn > 0 ? accessColumn + columnGap : 0)
    // × follows the last shown column; the spare room of a wide dock lies after it.
    readonly property real removeX: statusX + (statusColumn > 0 ? statusColumn + columnGap : 0)
    // The column group's right edge: the last shown column's (Status, rw, Type, else Value).
    readonly property real groupRight: removeX - columnGap
    // Rows end left of the overlay scrollbar, so × never sits under it.
    readonly property real scrollReserve: Math.max(watchScrollBar.implicitWidth, Type.spacing) + Type.tight

    // The interval list: Continuous where the provider accepts 0, then the
    // usual steps; a stored value outside them is shown as it is; last,
    // Other…, which takes any interval in ms.
    readonly property int intervalMinimum: provider.watchIntervalMinimumMs === undefined
                                           ? 1 : provider.watchIntervalMinimumMs
    readonly property int intervalMaximum: 60000
    readonly property var intervalSteps: [0, 100, 250, 500, 1000, 2000, 5000, 10000, 60000]
    readonly property var intervalChoices: {
        const value = provider.watchIntervalMs
        const steps = intervalSteps.filter(ms => ms >= intervalMinimum)
        if (steps.indexOf(value) < 0) {
            steps.push(value)
            steps.sort((a, b) => a - b)
        }
        return steps.map(ms => ({ ms: ms, text: intervalText(ms) }))
                    .concat([{ ms: -1, text: qsTr("Other…") }])
    }
    readonly property int intervalIndex: intervalChoices.findIndex(choice => choice.ms === provider.watchIntervalMs)
    function intervalText(ms) {
        if (ms === 0)
            return qsTr("Continuous")
        if (ms === 60000)
            return qsTr("1 min")
        if (ms >= 1000)
            return qsTr("%L1 s").arg(ms / 1000)
        return qsTr("%1 ms").arg(ms)
    }

    // No horizontal padding (WL-9 E1): row fills, stripes and the header rule
    // span the pane; text stands on one left line L (Type.padding) and the
    // toolbar ends on R (the scroll bar's edge).
    topPadding: Type.padding
    bottomPadding: Type.padding
    leftPadding: 0
    rightPadding: 0
    implicitWidth: Type.bodyPixels * 30
    background: Rectangle {
        color: panel.framed ? Theme.surface : Theme.surfaceWindow
        radius: panel.framed ? Type.radius * 2 : 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Type.tight

        RowLayout {
            id: bar
            objectName: "watchToolbar"
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: panel.scrollReserve
            Layout.minimumHeight: Type.controlHeight
            spacing: panel.barGap
            // The title, which folds the list. One state, two ways in: a host
            // may set `expanded` and the title shows it; a click sets it.
            FoldTitle {
                id: fold
                objectName: "watchlistExpanded"
                visible: panel.collapsible
                text: qsTr("Watchlist")
                countText: qsTr("%L1").arg(panel.count)
                checked: panel.expanded
                onCheckedChanged: if (panel.expanded !== checked) panel.expanded = checked
            }
            Button {
                id: readOnce
                objectName: "refreshWatchButton"
                readonly property real barWidth: implicitContentWidth + leftPadding + rightPadding
                Layout.preferredWidth: barWidth
                Layout.preferredHeight: Type.controlHeight
                verticalPadding: 0
                visible: panel.readOnceInBar
                text: qsTr("Read once")
                enabled: panel.canRead
                onClicked: panel.requestStart(false)
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("Read every watched Field once")
            }
            Button {
                id: start
                objectName: "startWatchButton"
                // As wide as the longer of its two words, so it does not jump.
                readonly property real barWidth: Math.max(startMetrics.advanceWidth, stopMetrics.advanceWidth)
                                                 + leftPadding + rightPadding
                TextMetrics { id: startMetrics; font: start.font; text: qsTr("Start") }
                TextMetrics { id: stopMetrics; font: start.font; text: qsTr("Stop") }
                Layout.preferredWidth: barWidth
                Layout.preferredHeight: Type.controlHeight
                verticalPadding: 0
                text: panel.active ? qsTr("Stop") : qsTr("Start")
                enabled: panel.active || (panel.connected && panel.count > 0 && panel.readable)
                onClicked: panel.active ? panel.requestStop() : panel.requestStart(true)
                Accessible.name: panel.active ? qsTr("Stop polling") : qsTr("Start polling")
            }
            Label {
                id: every
                objectName: "watchEvery"
                visible: panel.everyShown
                text: qsTr("Every")
                color: Theme.textMuted
                font.pointSize: Type.caption
            }
            // The interval's place: the list, or Other…'s field in its stead.
            // It says why while polling keeps it still, which a disabled list
            // under the pointer cannot.
            Item {
                id: intervalSlot
                Layout.preferredWidth: interval.barWidth
                Layout.preferredHeight: Type.controlHeight
                HoverHandler { id: intervalHover }
                ToolTip.visible: intervalHover.hovered && !interval.popup.visible
                ToolTip.delay: 700
                // Stopped after polling, the last cycle's time comes first: what
                // an interval can be chosen against.
                readonly property string lastCycle: !panel.paceKnown || panel.pace.polling !== true ? ""
                    : panel.pace.values === 1 ? qsTr("The last cycle of 1 value took %1.").arg(panel.durationText(panel.pace.lastCycleMs))
                    : qsTr("The last cycle of %L1 values took %2.").arg(panel.pace.values).arg(panel.durationText(panel.pace.lastCycleMs))
                readonly property string howOften: panel.provider.watchIntervalMs === 0
                    ? qsTr("Read again as soon as a read finishes, at low priority: other requests go first.")
                    : qsTr("How often to read. Fields are read one after another.")
                ToolTip.text: panel.active ? qsTr("Stop to change the interval.")
                            : intervalInput.visible ? qsTr("%L1 to %L2 ms, then Enter.").arg(panel.intervalMinimum).arg(panel.intervalMaximum)
                            : lastCycle.length > 0 ? lastCycle + " " + howOften
                            : howOften
                ComboBox {
                    id: interval
                    objectName: "watchInterval"
                    anchors.fill: parent
                    visible: !intervalInput.visible
                    model: panel.intervalChoices
                    textRole: "text"
                    implicitContentWidthPolicy: ComboBox.WidestText
                    readonly property real barWidth: implicitContentWidth + leftPadding + rightPadding
                    verticalPadding: 0
                    currentIndex: panel.intervalIndex
                    enabled: !panel.active
                    onActivated: index => {
                        const ms = panel.intervalChoices[index].ms
                        if (ms >= 0)
                            panel.provider.watchIntervalMs = ms
                        else
                            intervalInput.begin()
                    }
                    // Back to the provider's interval, whatever the list showed.
                    function resync() {
                        currentIndex = Qt.binding(() => panel.intervalIndex)
                    }
                    Accessible.name: qsTr("Polling interval")
                }
                // Other…: any interval, in milliseconds, in the list's place.
                // Enter sets it; Esc or a click elsewhere leaves it as it was.
                TextField {
                    id: intervalInput
                    objectName: "watchIntervalInput"
                    anchors.fill: parent
                    visible: false
                    topPadding: 0
                    bottomPadding: 0
                    rightPadding: unit.width + 2 * Type.spacing
                    validator: IntValidator { bottom: panel.intervalMinimum; top: panel.intervalMaximum }
                    inputMethodHints: Qt.ImhDigitsOnly
                    Accessible.name: qsTr("Polling interval in milliseconds")
                    Label {
                        id: unit
                        anchors.right: parent.right
                        anchors.rightMargin: Type.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("ms")
                        color: Theme.textMuted
                    }
                    function begin() {
                        text = String(panel.provider.watchIntervalMs)
                        visible = true
                        forceActiveFocus()
                        selectAll()
                    }
                    function finish() {
                        visible = false
                        interval.resync()
                        interval.forceActiveFocus()
                    }
                    onAccepted: {
                        panel.provider.watchIntervalMs = Number(text)
                        finish()
                    }
                    Keys.onEscapePressed: finish()
                    onActiveFocusChanged: if (!activeFocus && visible) {
                        visible = false
                        // The panel is a focus scope: let go, or it would give
                        // the keys back to this hidden field.
                        focus = false
                        interval.resync()
                    }
                }
            }
            // The pace in the bar, a group's gap after the interval it
            // qualifies (WL-9 D1); behind, the warning glyph before it. The
            // same words, tooltip and announcements as under the bar.
            ToolIcon {
                objectName: "watchBarStatusGlyph"
                visible: panel.paceInBar && panel.statusWarns
                Layout.leftMargin: Type.groupSpacing - panel.barGap
                Layout.preferredWidth: panel.markSize
                Layout.preferredHeight: panel.markSize
                Layout.alignment: Qt.AlignVCenter
                name: "warning"
                color: Theme.warning
                Accessible.ignored: true
            }
            Label {
                id: barStatus
                // The name of whichever of the two places shows the status.
                objectName: panel.paceInBar ? "watchStatusLabel" : "watchBarStatusLabel"
                visible: panel.paceInBar
                Layout.leftMargin: panel.statusWarns ? 0 : Type.groupSpacing - panel.barGap
                Layout.alignment: Qt.AlignVCenter
                textFormat: Text.PlainText
                text: panel.statusText
                color: Theme.textSecondary
                font.pointSize: Type.caption
                readonly property string tip: panel.paceText.length > 0 ? panel.paceTip : ""
                Accessible.description: tip
                HoverHandler { id: barStatusHover }
                PlainToolTip {
                    objectName: panel.paceInBar ? "watchPaceTip" : "watchBarPaceTip"
                    visible: barStatusHover.hovered && barStatus.tip.length > 0
                    delay: 700
                    text: barStatus.tip
                }
            }
            Item {
                Layout.fillWidth: true
            }
            // List | Tree, as FileViewer's Values | Hex: checkable, the chosen
            // one filled; Left and Right move between them.
            Row {
                id: viewSegments
                objectName: "watchViewSegments"
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("View")
                visible: panel.viewInBar
                Layout.preferredHeight: Type.controlHeight
                spacing: 0
                ButtonGroup { id: viewModes }
                Repeater {
                    id: viewButtons
                    model: [
                        { tree: false, text: qsTr("List"), name: qsTr("List view"),
                          tip: qsTr("List: one row per path") },
                        { tree: true, text: qsTr("Tree"), name: qsTr("Tree view"),
                          tip: qsTr("Tree: grouped by path, as in the Device tree") }
                    ]
                    delegate: Button {
                        id: segment
                        required property var modelData
                        objectName: modelData.tree ? "watchViewTree" : "watchViewList"
                        // As wide as its word, whatever the style's minimum.
                        width: implicitContentWidth + leftPadding + rightPadding
                        height: Type.controlHeight
                        verticalPadding: 0
                        font.pointSize: Type.caption
                        text: modelData.text
                        checkable: true
                        checked: panel.treeView === modelData.tree
                        flat: !checked
                        ButtonGroup.group: viewModes
                        // One Tab stop a group, the checked one; a segment keeps
                        // its stop while it has the keys, since a choice made
                        // elsewhere (an arrow, Undo, a project) cannot take the
                        // stop from the item the keys are on.
                        focusPolicy: checked || activeFocus ? Qt.StrongFocus : Qt.ClickFocus
                        Accessible.role: Accessible.RadioButton
                        onClicked: panel.showTree(modelData.tree)
                        Keys.onLeftPressed: {
                            panel.showTree(false)
                            viewButtons.itemAt(0).forceActiveFocus(Qt.TabFocusReason)
                        }
                        Keys.onRightPressed: {
                            panel.showTree(true)
                            viewButtons.itemAt(1).forceActiveFocus(Qt.TabFocusReason)
                        }
                        Accessible.name: modelData.name
                        Accessible.description: modelData.tip
                        ToolTip.visible: hovered || visualFocus
                        ToolTip.delay: 700
                        ToolTip.text: modelData.tip
                    }
                }
            }
            // Values shown in a form of their own, other than the Watchlist's
            // (WL-3c): how many, and a press shows every value as Original.
            // Before the format control in both its shapes; always in the row
            // and only clear when there is nothing to count, so its coming
            // and going moves nothing.
            Basic.ToolButton {
                id: ownFormats
                objectName: "watchOwnFormats"
                readonly property int rows: panel.ownFormatRows
                visible: panel.ownMarkShown
                opacity: rows > 0 ? 1 : 0
                // Held while it has the keys: a control the keys are on cannot
                // leave the Tab chain, so they go to the list first.
                enabled: rows > 0 || activeFocus
                onRowsChanged: if (rows === 0 && activeFocus) watchList.forceActiveFocus(Qt.TabFocusReason)
                Layout.leftMargin: panel.barGap
                Layout.preferredHeight: Type.controlHeight
                Layout.preferredWidth: implicitWidth
                FontMetrics { id: ownMetrics; font: ownFormats.font }
                // As wide as the most it can count, the Watchlist's rows.
                implicitWidth: {
                    void ownMetrics.font
                    return Math.ceil(ownMetrics.advanceWidth("\u25CF " + "9".repeat(String(Math.max(1, panel.count)).length)))
                           + leftPadding + rightPadding
                }
                padding: Type.tight
                focusPolicy: Qt.TabFocus
                font.pointSize: Type.caption
                text: "\u25CF " + rows.toLocaleString(Qt.locale(), "f", 0)
                contentItem: Text {
                    text: ownFormats.text
                    font: ownFormats.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: ownFormats.hovered ? Theme.textPrimary : Theme.textSecondary
                    Accessible.ignored: true
                }
                background: Rectangle {
                    radius: Type.radius
                    color: ownFormats.down ? Theme.statePressed : ownFormats.hovered ? Theme.stateHover : "transparent"
                    border.width: ownFormats.visualFocus ? 2 : 0
                    border.color: Theme.focusRing
                }
                onClicked: if (rows > 0) panel.resetFormats()
                readonly property string counted: rows === 1 ? qsTr("1 value shown in a format of its own")
                                                             : qsTr("%Ln values shown in a format of their own", "", rows)
                ToolTip.visible: rows > 0 && (hovered || visualFocus)
                ToolTip.delay: 700
                ToolTip.text: panel.format === "original"
                              ? qsTr("%1. Press to show every value as Original (Undo takes it back).").arg(counted)
                              : qsTr("%1. Press to show every value as Original, the Watchlist's format too (Undo takes it back).").arg(counted)
                Accessible.role: Accessible.Button
                Accessible.ignored: rows === 0
                Accessible.name: qsTr("Show every value as Original (%1)").arg(counted)
            }
            // Original | Dec | Hex, as List | Tree.
            Row {
                id: formatSegments
                objectName: "watchFormatSegments"
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("Number format")
                visible: panel.formatInBar
                // A gap from List | Tree: two choices, not one of five.
                Layout.leftMargin: panel.barGap
                Layout.preferredHeight: Type.controlHeight
                spacing: 0
                ButtonGroup { id: formatModes }
                Repeater {
                    id: formatButtons
                    model: panel.formats
                    delegate: Button {
                        required property var modelData
                        required property int index
                        objectName: "watchFormat_" + modelData.name
                        width: implicitContentWidth + leftPadding + rightPadding
                        height: Type.controlHeight
                        verticalPadding: 0
                        font.pointSize: Type.caption
                        text: modelData.text
                        checkable: true
                        checked: panel.format === modelData.name
                        flat: !checked
                        ButtonGroup.group: formatModes
                        // One Tab stop a group, the checked one; a segment keeps
                        // its stop while it has the keys, since a choice made
                        // elsewhere (an arrow, Undo, a project) cannot take the
                        // stop from the item the keys are on.
                        focusPolicy: checked || activeFocus ? Qt.StrongFocus : Qt.ClickFocus
                        Accessible.role: Accessible.RadioButton
                        onClicked: panel.setFormat(modelData.name)
                        Keys.onLeftPressed: if (index > 0) {
                            panel.setFormat(panel.formats[index - 1].name)
                            formatButtons.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason)
                        }
                        Keys.onRightPressed: if (index < panel.formats.length - 1) {
                            panel.setFormat(panel.formats[index + 1].name)
                            formatButtons.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason)
                        }
                        Accessible.name: qsTr("%1 format").arg(modelData.text)
                        Accessible.description: modelData.tip
                        ToolTip.visible: hovered || visualFocus
                        ToolTip.delay: 700
                        ToolTip.text: modelData.tip
                    }
                }
            }
            // Too narrow for three: one button showing the form, "Hex ▾", as
            // wide as its widest word.
            Button {
                id: formatButton
                objectName: "watchFormatButton"
                visible: panel.formatAvailable && !panel.formatInBar
                Layout.leftMargin: panel.barGap
                FontMetrics { id: formatMetrics; font: formatButton.font }
                implicitWidth: {
                    void formatMetrics.font
                    return Math.ceil(Math.max(...panel.formats.map(item => formatMetrics.advanceWidth(item.text + " ▾"))))
                           + leftPadding + rightPadding
                }
                Layout.preferredHeight: Type.controlHeight
                verticalPadding: 0
                flat: true
                font.pointSize: Type.caption
                text: panel.formatWord(panel.format) + " ▾"
                onClicked: formatMenu.popup(formatButton, 0, formatButton.height)
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: qsTr("Number format: %1").arg(panel.formatWord(panel.format))
                ToolTip.visible: hovered && !formatMenu.visible
                ToolTip.delay: 700
                ToolTip.text: qsTr("How the Watchlist shows its numbers")
                Menu {
                    id: formatMenu
                    objectName: "watchFormatMenu"
                    property bool differ: false
                    onAboutToShow: differ = panel.formatsDiffer()
                    ButtonGroup { id: formatItems; exclusive: true }
                    Repeater {
                        model: panel.formats
                        MenuItem {
                            required property var modelData
                            objectName: "watchFormatItem_" + modelData.name
                            text: modelData.text
                            checkable: true
                            checked: panel.format === modelData.name
                            ButtonGroup.group: formatItems
                            Accessible.description: modelData.tip
                            onTriggered: panel.setFormat(modelData.name)
                        }
                    }
                    MenuSeparator {
                        visible: panel.formatsResettable
                        height: visible ? implicitHeight : 0
                    }
                    MenuItem {
                        objectName: "watchFormatReset"
                        visible: panel.formatsResettable
                        height: visible ? implicitHeight : 0
                        enabled: visible && formatMenu.differ
                        leftPadding: panel.checkColumn(formatMenu.count > 0 ? formatMenu.itemAt(0) : null, horizontalPadding)
                        text: qsTr("Reset number formats to Original")
                        onTriggered: panel.resetFormats()
                    }
                }
            }
            Basic.ToolButton {
                id: more
                objectName: "watchMoreButton"
                implicitWidth: Type.controlHeight
                implicitHeight: Type.controlHeight
                padding: 0
                focusPolicy: Qt.TabFocus
                text: qsTr("More Watchlist actions")
                display: AbstractButton.IconOnly
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent
                        spacing: dot
                        readonly property int dot: Math.max(2, Math.round(Type.bodyPixels * 0.2))
                        Repeater {
                            model: 3
                            Rectangle {
                                required property int index
                                width: parent.dot
                                height: parent.dot
                                radius: parent.dot / 2
                                color: more.enabled ? Theme.textPrimary : Theme.textDisabled
                            }
                        }
                    }
                }
                background: Rectangle {
                    radius: Type.radius
                    color: more.down || moreMenu.visible ? Theme.statePressed : more.hovered ? Theme.stateHover : "transparent"
                    border.width: more.visualFocus ? 2 : 0
                    border.color: Theme.focusRing
                }
                onClicked: moreMenu.popup(more, 0, more.height)
                ToolTip.visible: (hovered || visualFocus) && !moreMenu.visible
                ToolTip.delay: 700
                ToolTip.text: text
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: text
            }
        }

        Menu {
            id: moreMenu
            objectName: "watchMoreMenu"
            readonly property bool readManySupported: panel.provider.watchReadManySupported !== false
            readonly property bool explicitIndividual: panel.provider.watchRequiresExplicitIndividual === true
            // Without ReadMany, and without a provider that wants the choice
            // made, Fields are read one by one and there is nothing to choose.
            readonly property bool choosable: readManySupported || explicitIndividual
            readonly property bool individual: !choosable || panel.provider.watchIndividual === true
            // Whether any value is shown other than as Original, as the menu opens.
            property bool formatsDiffer: false
            onAboutToShow: formatsDiffer = panel.formatsDiffer()
            // As wide as its widest item, whatever the style's own width; the
            // plain items keep the check column, so every label lines up.
            readonly property real widestItem: {
                let widest = 0
                for (let i = 0; i < count; ++i) {
                    const item = itemAt(i)
                    if (item && item.height > 0)
                        widest = Math.max(widest, item.implicitWidth)
                }
                return widest
            }
            width: Math.max(implicitWidth, widestItem + leftPadding + rightPadding)
            // A style that draws the check inside the item's left padding has
            // kept the column there already (TelemetryDark); one that draws it
            // at the padding's edge pushes the label past it (Basic).
            readonly property real checkColumn: readManyItem.leftPadding
                + (readManyItem.indicator && readManyItem.indicator.x >= readManyItem.leftPadding
                   ? readManyItem.indicator.width + readManyItem.spacing : 0)
            MenuItem {
                objectName: "refreshWatchItem"
                visible: !panel.readOnceInBar
                height: panel.readOnceInBar ? 0 : implicitHeight
                leftPadding: moreMenu.checkColumn
                text: qsTr("Read once")
                // Off while hidden: the menu's arrows do not skip a hidden item.
                enabled: panel.canRead && !panel.readOnceInBar
                onTriggered: panel.requestStart(false)
            }
            MenuItem {
                objectName: "addWatchItem"
                leftPadding: moreMenu.checkColumn
                text: qsTr("Watch the selected Field")
                enabled: panel.provider.canAddWatch === true && !panel.changeLocked
                onTriggered: {
                    // Through the property: setting the title's state would
                    // break its binding to `expanded`.
                    if (panel.provider.addWatch())
                        panel.expanded = true
                }
            }
            // Read and Watch, then how the rows are shown.
            MenuSeparator {
                visible: panel.formatsResettable || (panel.treeAvailable && !panel.viewInBar)
                         || (panel.sortable && !panel.treeView)
                height: visible ? implicitHeight : 0
            }
            MenuItem {
                objectName: "watchViewListItem"
                readonly property bool shown: panel.treeAvailable && !panel.viewInBar
                visible: shown
                height: shown ? implicitHeight : 0
                enabled: shown
                checkable: true
                ButtonGroup.group: viewItems
                checked: !panel.treeView
                text: qsTr("List view")
                onTriggered: panel.showTree(false)
            }
            MenuItem {
                objectName: "watchViewTreeItem"
                readonly property bool shown: panel.treeAvailable && !panel.viewInBar
                visible: shown
                height: shown ? implicitHeight : 0
                enabled: shown
                checkable: true
                ButtonGroup.group: viewItems
                checked: panel.treeView
                text: qsTr("Tree view")
                onTriggered: panel.showTree(true)
            }
            MenuItem {
                objectName: "watchExpandAll"
                visible: panel.treeView
                height: panel.treeView ? implicitHeight : 0
                enabled: panel.treeView && panel.rows.count > 0
                leftPadding: moreMenu.checkColumn
                text: qsTr("Expand all")
                onTriggered: panel.rows.expandAll()
            }
            MenuItem {
                objectName: "watchCollapseAll"
                visible: panel.treeView
                height: panel.treeView ? implicitHeight : 0
                enabled: panel.treeView && panel.rows.count > 0
                leftPadding: moreMenu.checkColumn
                text: qsTr("Collapse all")
                onTriggered: panel.rows.collapseAll()
            }
            MenuItem {
                objectName: "watchSortByName"
                readonly property bool shown: panel.sortable && !panel.treeView
                visible: shown
                height: shown ? implicitHeight : 0
                enabled: shown
                checkable: true
                checked: panel.sorted
                text: qsTr("Sort by name")
                onTriggered: panel.setSorted(!panel.sorted)
            }
            MenuItem {
                objectName: "watchResetFormats"
                visible: panel.formatsResettable
                height: panel.formatsResettable ? implicitHeight : 0
                enabled: panel.formatsResettable && moreMenu.formatsDiffer
                leftPadding: moreMenu.checkColumn
                text: qsTr("Reset number formats to Original")
                onTriggered: panel.resetFormats()
            }
            MenuSeparator {}
            MenuItem {
                id: readManyItem
                objectName: "watchModeReadMany"
                visible: moreMenu.choosable
                height: moreMenu.choosable ? implicitHeight : 0
                ButtonGroup.group: modes
                checkable: true
                checked: !moreMenu.individual
                enabled: moreMenu.choosable && !panel.active
                text: moreMenu.readManySupported ? qsTr("Read with ReadMany") : qsTr("Read with ReadMany (unavailable)")
                onTriggered: panel.provider.watchIndividual = false
            }
            MenuItem {
                objectName: "watchModeFieldRead"
                ButtonGroup.group: modes
                checkable: true
                checked: moreMenu.individual
                enabled: moreMenu.choosable && !panel.active
                text: qsTr("Read each Field with FieldRead")
                onTriggered: panel.provider.watchIndividual = true
            }
            MenuSeparator {}
            MenuItem {
                objectName: "clearWatchItem"
                leftPadding: moreMenu.checkColumn
                text: qsTr("Clear the Watchlist")
                enabled: panel.count > 0 && !panel.changeLocked
                onTriggered: panel.clearAll()
            }
        }

        // The status under the toolbar, wrapped: the pace when the bar cannot
        // hold it whole beside every other control (WL-9 D1), a wait, a
        // failure's advice read whole however long; the toolbar does not move
        // as it changes. Behind its interval, the warning glyph precedes it.
        RowLayout {
            id: statusRow
            objectName: "watchStatusRow"
            visible: panel.statusLineShown && !panel.paceInBar
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: panel.scrollReserve
            spacing: Type.spacing
            ToolIcon {
                objectName: "watchStatusGlyph"
                visible: panel.statusWarns
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Math.max(0, (statusLineMetrics.height - height) / 2)
                Layout.preferredWidth: panel.markSize
                Layout.preferredHeight: panel.markSize
                name: "warning"
                color: Theme.warning
                Accessible.ignored: true
            }
            Label {
                id: statusLine
                objectName: panel.paceInBar ? "watchLineStatusLabel" : "watchStatusLabel"
                Layout.fillWidth: true
                // The height its text takes, wrapped: the layout missed this
                // label's implicit height growing (a bigger font, a glyph from a
                // fallback font) and cut the last line.
                Layout.preferredHeight: Math.ceil(contentHeight)
                textFormat: Text.PlainText
                text: panel.statusText
                color: panel.readManyAdvice ? Theme.warning : Theme.textSecondary
                font.pointSize: Type.caption
                wrapMode: Text.Wrap
                readonly property string tip: panel.paceText.length > 0 ? panel.paceTip : ""
                Accessible.description: tip
                FontMetrics { id: statusLineMetrics; font: statusLine.font }
                HoverHandler { id: statusHover }
                PlainToolTip {
                    objectName: panel.paceInBar ? "watchLinePaceTip" : "watchPaceTip"
                    visible: statusHover.hovered && statusLine.tip.length > 0
                    delay: 700
                    text: statusLine.tip
                }
            }
        }
        // What was done last, in one line (WL-9 D1): a row's write ("Wrote
        // 22:31:05 · device.Config.Gain = 2 · read back 2"), why a row can't be
        // written, or what the panel did ("Removed 3 values of device.Config ·
        // Undo"). The newer event wins. A failure has the error glyph, a doubt
        // the warning glyph, its words in the primary colour; a confirmed
        // write's time flashes once. What a newer event pushes out, the row
        // still shows.
        RowLayout {
            id: writeLine
            objectName: "watchWriteLine"
            visible: panel.open && panel.lastEvent === "write" && writeOutcome.text.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: panel.scrollReserve
            spacing: Type.spacing
            ErrorGlyph {
                objectName: "watchWriteGlyph"
                visible: writeOutcome.kind === "refused"
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Math.max(0, (writeOutcome.lineHeightPixels - height) / 2)
            }
            ToolIcon {
                objectName: "watchWriteWarning"
                visible: writeOutcome.kind === "differs" || writeOutcome.kind === "unconfirmed"
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Math.max(0, (writeOutcome.lineHeightPixels - height) / 2)
                Layout.preferredWidth: panel.markSize
                Layout.preferredHeight: panel.markSize
                name: "warning"
                color: Theme.warning
                Accessible.ignored: true
            }
            Label {
                id: writeOutcome
                objectName: "watchWriteOutcome"
                Layout.fillWidth: true
                // Its two lines at most from its implicit height: a height set
                // from the text it shows would cut it to the first line.
                Layout.minimumWidth: 0
                readonly property real lineHeightPixels: writeMetrics.height
                textFormat: Text.PlainText
                text: panel.writeLineText
                readonly property string kind: panel.writeRefusalText.length > 0 ? "refused"
                    : typeof panel.provider.watchWriteKind === "string" ? panel.provider.watchWriteKind : ""
                readonly property bool failed: kind === "refused" || kind === "differs" || kind === "unconfirmed"
                color: failed ? Theme.textPrimary : Theme.textSecondary
                font.pointSize: Type.caption
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                Accessible.name: text
                FontMetrics { id: writeMetrics; font: writeOutcome.font }
                HoverHandler { id: writeHover }
                PlainToolTip { visible: writeHover.hovered && writeOutcome.truncated; text: writeOutcome.text; delay: 700 }
                // The time, flashed once in the accent when a write is read back
                // as written: the same glyphs over the line's own, fading out.
                Label {
                    id: writeTime
                    objectName: "watchWriteTime"
                    readonly property var found: /\b\d{1,2}:\d{2}:\d{2}\b/.exec(writeOutcome.text)
                    text: found ? found[0] : ""
                    x: found ? Math.round(writePrefix.advanceWidth) : 0
                    y: 0
                    height: writeOutcome.lineHeightPixels
                    textFormat: Text.PlainText
                    font: writeOutcome.font
                    color: Theme.accent
                    opacity: 0
                    visible: text.length > 0 && writePrefix.advanceWidth < writeOutcome.width
                    Accessible.ignored: true
                    TextMetrics {
                        id: writePrefix
                        font: writeOutcome.font
                        text: writeTime.found ? writeOutcome.text.substring(0, writeTime.found.index) : ""
                    }
                }
                OpacityAnimator {
                    id: writeTimeFlash
                    target: writeTime
                    from: 1
                    to: 0
                    duration: 600
                    easing.type: Easing.InQuad
                }
            }
            LinkButton {
                objectName: "watchAllowChanges"
                visible: panel.writeRefusalText.length > 0 && panel.writeRefusalChanges &&
                         panel.provider.allowMutations === false
                Layout.alignment: Qt.AlignTop
                font.pointSize: Type.caption
                text: qsTr("Allow changes")
                onClicked: {
                    panel.provider.allowMutations = true
                    panel.writeRefusalText = ""
                    panel.writeRefusalChanges = false
                }
            }
        }
        // Qt Quick has no clipboard API; a hidden editor copies its text.
        TextEdit { id: clipboardHelper; visible: false; textFormat: TextEdit.PlainText }
        // What the last removal, copy or format did, as the Device tree says
        // it: "Removed 3 values of device.Config · Undo", Undo while that step
        // is the one Undo would take; "Copied device.Config.Gain". It speaks
        // for the Watchlist's steps undone or redone anywhere. The same line
        // as a write's outcome: whichever came last shows.
        UndoNotice {
            id: notice
            objectName: "watchNotice"
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: panel.scrollReserve
            active: panel.open
            visible: active && text.length > 0 && panel.lastEvent !== "write"
            history: panel.history
            kinds: ["watch.rows", "watch.format"]
            revision: panel.revision
            onMessageChanged: if (message.length > 0) panel.lastEvent = "notice"
        }
        // Column titles over the rows, each at its column's x on every row;
        // "Name" over the depth-0 names. They stay while the rows scroll, a
        // group's gap below the lines above them (WL-9 E2).
        Item {
            id: columnTitles
            objectName: "watchColumns"
            visible: panel.open && panel.count > 0
            Layout.fillWidth: true
            Layout.topMargin: Type.groupSpacing - Type.tight
            Layout.preferredHeight: nameHeader.implicitHeight
            // The list by name, A-Z, and back to the order added (WL-2c); the
            // tree keeps the device's order, so its header only names.
            Label {
                id: nameHeader
                objectName: "watchColumnName"
                readonly property bool sortable: panel.sortable && !panel.treeView
                text: sortable && panel.sorted ? qsTr("Name") + " ▴" : qsTr("Name")
                color: nameHeaderHover.hovered && sortable ? Theme.textPrimary : Theme.textSecondary
                font.pointSize: Type.caption
                // A target as tall as a row; the text stays on the names' x.
                leftPadding: Type.tight
                rightPadding: Type.tight
                topPadding: Type.tight
                bottomPadding: Type.tight
                x: panel.nameXAt(0) - leftPadding
                width: Math.min(implicitWidth, Math.max(0, panel.valueX - panel.columnGap - x))
                elide: Text.ElideRight
                background: Rectangle {
                    radius: Type.radius
                    color: nameHeaderTap.pressed ? Theme.statePressed
                         : nameHeaderHover.hovered && nameHeader.sortable ? Theme.stateHover : "transparent"
                }
                HoverHandler { id: nameHeaderHover; enabled: nameHeader.sortable }
                TapHandler {
                    id: nameHeaderTap
                    enabled: nameHeader.sortable
                    onTapped: panel.setSorted(!panel.sorted)
                }
                PlainToolTip {
                    visible: nameHeaderHover.hovered && nameHeader.sortable
                    delay: 700
                    text: panel.sorted ? qsTr("Sorted by name, A-Z · click for the order added")
                                       : qsTr("Sort by name, A-Z")
                }
                Accessible.role: Accessible.ColumnHeader
                Accessible.name: sortable && panel.sorted ? qsTr("Name, sorted A to Z") : qsTr("Name")
                Accessible.description: sortable ? (panel.sorted ? qsTr("Press for the order added") : qsTr("Press to sort by name, A to Z")) : ""
                Accessible.onPressAction: if (sortable) panel.setSorted(!panel.sorted)
            }
            ColumnTitle {
                objectName: "watchColumnValue"
                x: panel.valueX
                width: panel.valueColumn
                text: qsTr("Value")
                horizontalAlignment: Text.AlignRight
            }
            ColumnTitle {
                objectName: "watchColumnType"
                visible: panel.typeColumn > 0
                x: panel.typeX
                width: panel.typeColumn
                text: qsTr("Type")
                Accessible.description: panel.typeScopesCut ? watchTypeTitleTip.text : ""
                HoverHandler { id: watchTypeTitleHover }
                PlainToolTip {
                    id: watchTypeTitleTip
                    objectName: "watchTypeHeaderToolTip"
                    visible: watchTypeTitleHover.hovered && panel.typeScopesCut
                    text: qsTr("Scopes are hidden for room; hover a type for its full name.")
                    delay: 700
                }
            }
            ColumnTitle {
                objectName: "watchColumnAccess"
                visible: panel.accessColumn > 0
                x: panel.accessX
                width: panel.accessColumn
                text: qsTr("rw")
                Accessible.name: qsTr("Access")
                ToolTip.visible: accessTitleHover.hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("Access: r can be read, w can be written")
                HoverHandler { id: accessTitleHover }
            }
            ColumnTitle {
                objectName: "watchColumnStatus"
                visible: panel.statusColumn > 0
                x: panel.statusX
                width: panel.statusColumn
                text: panel.statusGlyph ? "" : qsTr("Status")
            }
        }
        // The rule under the titles spans the pane, as the row fills do.
        Rectangle {
            objectName: "watchColumnsRule"
            visible: panel.open && panel.count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.outlineVariant
        }

        // Folded, the list keeps its place at no height rather than hiding:
        // hidden, then shown with its model in the same change, this panel made
        // its rows twice and a held row was destroyed (Qt 6.12, the inspector's
        // check; not reproduced in a smaller case).
        ListView {
            id: watchList
            Accessible.role: panel.treeView ? Accessible.Tree : Accessible.List
            onCurrentIndexChanged: {
                guideTimer.restart()
                if (panel.writeRefusalLeft)
                    return
                panel.writeRefusalText = ""
                panel.writeRefusalChanges = false
            }
            objectName: "watchlistView"
            Layout.fillWidth: true
            // A mouse drag takes rows onto the Plot or elsewhere (LP-1g3), as
            // in a file manager; the wheel, a touchpad and the scroll bar
            // scroll, and a touch screen still flicks.
            acceptedButtons: Qt.NoButton
            // With the "+" row the list is as tall as its rows, the row right
            // under the last one; past the room it scrolls. It fills, up to its
            // rows: a layout keeps an item that does not fill at its preferred
            // height, so a list as tall as many rows would never shrink to the
            // room and never scroll.
            Layout.fillHeight: panel.open
            Layout.preferredHeight: !panel.open ? 0 : panel.addAvailable ? Math.max(1, contentHeight)
                                                                     : Math.max(120, Type.rowHeight * 5)
            Layout.maximumHeight: panel.open && panel.addAvailable ? Math.max(1, contentHeight) : Number.POSITIVE_INFINITY
            Layout.minimumHeight: panel.open && panel.addAvailable ? Math.min(contentHeight, Type.rowHeight * 3) : 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // Rows are made as they come into view, at once. With the default
            // cache buffer the rows past the edge were made in the background,
            // and a row still being made when the rows changed was dropped half
            // made: Qt 6.12 warned "DelegateModel::cancel: index out range" when
            // the list emptied, or kept counting the row's controls as being
            // created until the engine closed. The client's emulated-device
            // check failed 21 runs in 60 that way.
            cacheBuffer: 0
            model: !panel.open ? null : panel.rows ? panel.rows : panel.provider.watchlist
            // The keyboard: arrows move the current row, Delete removes it,
            // F2 or Enter writes a writable one.
            activeFocusOnTab: panel.open
            // Every key that moves is the list's own (Keys.onPressed), so the
            // selection follows; none moves while a row is edited.
            keyNavigationEnabled: false
            currentIndex: -1
            // In: the cursor on the first selected row, the selection kept;
            // with none, the first row.
            onActiveFocusChanged: {
                if (!activeFocus || currentIndex >= 0 || count === 0)
                    return
                const selected = panel.selectable ? panel.rows.selectedRows() : []
                if (selected.length > 0)
                    currentIndex = selected[0]
                else
                    panel.moveTo(0, "select")
            }
            // Copies are the list's own while it has the keys; Ctrl+Z is the
            // whole panel's.
            Keys.onShortcutOverride: event => {
                if (event.key === Qt.Key_C && currentIndex >= 0 && (event.modifiers === Qt.ControlModifier
                        || event.modifiers === (Qt.AltModifier | Qt.ShiftModifier)))
                    event.accepted = true
                if (event.key === Qt.Key_A && event.modifiers === Qt.ControlModifier && panel.selectable
                        && !(currentItem && currentItem.editing))
                    event.accepted = true
                if (event.key === Qt.Key_P && event.modifiers === Qt.ControlModifier && currentIndex >= 0 && panel.plotTraces)
                    event.accepted = true
                if (event.key === Qt.Key_B && event.modifiers === Qt.ControlModifier && currentIndex >= 0 && panel.batchAvailable)
                    event.accepted = true
            }
            Keys.onPressed: event => {
                // Insert from anywhere in the list, or Down past the last row: the "+" row.
                const editing = currentItem !== null && currentItem.editing === true
                if (panel.addAvailable && !editing && (event.key === Qt.Key_Insert
                        || (event.key === Qt.Key_Down && event.modifiers === Qt.NoModifier && currentIndex === count - 1))) {
                    addRow.begin()
                    event.accepted = true
                    return
                }
                // While a row is edited its keys are the editor's alone.
                if (editing)
                    return
                // Moving selects; Shift extends from the anchor, Ctrl moves
                // the cursor alone.
                if (count > 0) {
                    const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                    const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                    const how = shift ? "extend" : ctrl ? "cursor" : "select"
                    const page = Math.max(1, Math.floor(height / Type.rowHeight) - 1)
                    const at = Math.max(0, currentIndex)
                    let to = -1
                    if (event.key === Qt.Key_Up)
                        to = currentIndex < 0 ? 0 : Math.max(0, at - 1)
                    else if (event.key === Qt.Key_Down)
                        to = currentIndex < 0 ? 0 : Math.min(count - 1, at + 1)
                    else if (event.key === Qt.Key_Home)
                        to = 0
                    else if (event.key === Qt.Key_End)
                        to = count - 1
                    else if (event.key === Qt.Key_PageUp)
                        to = Math.max(0, at - page)
                    else if (event.key === Qt.Key_PageDown)
                        to = Math.min(count - 1, at + page)
                    if (to >= 0 && (event.modifiers & ~(Qt.ShiftModifier | Qt.ControlModifier | Qt.KeypadModifier)) === 0) {
                        panel.moveTo(to, how)
                        event.accepted = true
                        return
                    }
                    if (panel.selectable && event.key === Qt.Key_A && event.modifiers === Qt.ControlModifier) {
                        panel.rows.selectAll()
                        panel.selectionGesture()
                        event.accepted = true
                        return
                    }
                    if (panel.selectable && currentIndex >= 0 && event.key === Qt.Key_Space
                            && event.modifiers === Qt.ControlModifier) {
                        panel.rows.select(currentIndex, true)
                        panel.selectionGesture()
                        event.accepted = true
                        return
                    }
                    if (panel.selectable && currentIndex >= 0 && event.key === Qt.Key_Escape
                            && event.modifiers === Qt.NoModifier
                            && (panel.selectedCount > 1 || !panel.rows.isSelected(currentIndex))) {
                        panel.rows.select(currentIndex)
                        panel.selectionGesture()
                        event.accepted = true
                        return
                    }
                }
                if (currentIndex < 0)
                    return
                // Esc steps out: after the selection, a held text or a
                // doubtful write's mark on the current row goes (WL-9 C2).
                if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) {
                    event.accepted = panel.dropRowHeld(currentItem)
                    return
                }
                if (event.key === Qt.Key_C && event.modifiers === Qt.ControlModifier) {
                    event.accepted = panel.copyRows(currentIndex, "lines")
                    return
                }
                if (event.key === Qt.Key_C && event.modifiers === (Qt.AltModifier | Qt.ShiftModifier)) {
                    event.accepted = panel.copyRows(currentIndex, "paths")
                    return
                }
                // On the live Plot, or off it: the selection, a node's values.
                if (event.key === Qt.Key_P && event.modifiers === Qt.ControlModifier) {
                    event.accepted = panel.togglePlot(currentIndex)
                    return
                }
                // Into the Batch: the selection, a node's values (16.4).
                if (event.key === Qt.Key_B && event.modifiers === Qt.ControlModifier && panel.batchAvailable) {
                    panel.batchRows(currentIndex)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)) {
                    if (currentItem)
                        panel.openRowMenu(currentIndex, currentItem, Type.padding, currentItem.height)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Delete && !panel.changeLocked) {
                    panel.removeAt(currentIndex)
                    event.accepted = true
                } else if (panel.treeView && currentItem
                           && (event.key === Qt.Key_Right || event.key === Qt.Key_Left)) {
                    // The Device tree's keys: Right opens, then steps in; Left
                    // folds, then steps out.
                    if (event.key === Qt.Key_Right) {
                        if (currentItem.branch && !currentItem.expanded)
                            panel.rows.setExpanded(currentIndex, true)
                        else if (currentItem.branch && currentIndex + 1 < count)
                            panel.moveTo(currentIndex + 1, "select")
                    } else if (currentItem.branch && currentItem.expanded) {
                        panel.rows.setExpanded(currentIndex, false)
                    } else if (panel.rows.parentRow(currentIndex) >= 0) {
                        panel.moveTo(panel.rows.parentRow(currentIndex), "select")
                    }
                    event.accepted = true
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                           && currentItem && currentItem.branch) {
                    panel.rows.setExpanded(currentIndex, !currentItem.expanded)
                    event.accepted = true
                } else if ((event.key === Qt.Key_F2 || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                           && currentItem && currentItem.writable) {
                    currentItem.startEdit()
                    event.accepted = true
                } else if (event.key === Qt.Key_F2 && currentItem && currentItem.isValue) {
                    panel.sayNotWritable(currentItem)
                    event.accepted = true
                }
            }
            ScrollBar.vertical: ScrollBar {
                id: watchScrollBar
                objectName: "watchlistScrollBar"
                policy: ScrollBar.AsNeeded
            }
            // A focus scope: the list gives its current row the focus again
            // when rows come in above it, and the row hands it to its open
            // editor instead of taking it away.
            delegate: FocusScope {
                id: watchRow
                objectName: "watchlistRow"
                required property int index
                required property string name
                required property string status
                required property string text
                // Optional roles (writable, sampleValue, sampleExact, the WL-9
                // ones) are read through the model object, so other providers
                // need not have them.
                required property var model
                width: watchList.width
                // Dragged by the mouse past the threshold: its values' paths go
                // with it, to whatever accepts them.
                property var dragged: []
                Drag.dragType: Drag.Automatic
                Drag.supportedActions: Qt.CopyAction
                Drag.active: rowDrag.active
                Drag.mimeData: ({"application/x-telemetry-paths": JSON.stringify({from: "watchlist", paths: watchRow.dragged}),
                                "text/plain": watchRow.dragged.join("\n")})
                // The paths are taken at the press, before the drag starts.
                PointHandler {
                    acceptedButtons: Qt.LeftButton
                    onActiveChanged: if (active) watchRow.dragged = panel.dragPaths(watchRow.index)
                }
                DragHandler {
                    id: rowDrag
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    enabled: !watchRow.editing
                }
                // A writable Field is changed in its own row, as in Live
                // Expressions: double-click the value (or F2), Enter writes,
                // Esc cancels; Tab or a click away writes, or holds text that
                // is no value.
                readonly property bool writable: model.writable === true &&
                                                 panel.provider.canWriteWatch === true
                // The value takes text at all: its editor stays made while a
                // write runs, so the typed value, a held text and a confirmed
                // read-back's flash show in it.
                readonly property bool editableRow: model.writable === true
                // What the row is: a value, or in the tree a node above values
                // (a group, a struct, an array, an element of one).
                readonly property string node: typeof model.node === "string" ? model.node : "value"
                readonly property bool isValue: node === "value"
                readonly property bool isGroup: node === "group"
                readonly property int depth: typeof model.depth === "number" ? model.depth : 0
                readonly property bool branch: model.branch === true
                readonly property bool expanded: model.expanded === true
                readonly property int leaves: typeof model.leaves === "number" ? model.leaves : 1
                // Inside a Field in the tree: one read, one outcome, shown on
                // the Field; its members leave Status blank and show its rw.
                readonly property bool nested: model.nested === true
                readonly property int sourceRow: typeof model.sourceRow === "number" ? model.sourceRow : index
                // The cell is open: the same ValueCell as Details'.
                readonly property bool editing: cellLoader.item ? cellLoader.item.open : false
                readonly property bool cellShown: cellLoader.visible
                // A value inside a struct or array Field is written by writing
                // its whole Field: what that covers, and why it can't be.
                readonly property var writeScope: model.writeScope && typeof model.writeScope.field === "string"
                                                  && model.writeScope.field.length > 0 ? model.writeScope : null
                readonly property string writeRefusal: typeof model.writeRefusal === "string" ? model.writeRefusal : ""
                // The write reads the Field again first: the other values keep
                // what the device holds then, not what the row last showed. One
                // quiet line under the cell says so, every time a member is
                // edited; the long explanation is that line's tooltip.
                function tail(path) { return path.substring(Math.max(path.lastIndexOf("."), path.lastIndexOf("/")) + 1) }
                readonly property string scopeNote: {
                    if (!writeScope)
                        return ""
                    const total = writeScope.others + 1
                    const field = tail(writeScope.field)
                    switch (writeScope.kind) {
                    case "elements": return qsTr("Writes all %L1 elements of %2.").arg(total).arg(field)
                    case "values": return qsTr("Writes all %L1 values of %2.").arg(total).arg(field)
                    default: return qsTr("Writes all %L1 members of %2.").arg(total).arg(field)
                    }
                }
                readonly property string scopeDetail: writeScope
                    ? qsTr("Enter reads %1, sets %2 in it and writes all %L3 members back; the other %L4 keep the values the device holds then. If the device changes one of them in between, that change is overwritten.")
                          .arg(tail(writeScope.field)).arg(watchRow.name.substring(nameCell.leafStart)).arg(writeScope.others + 1).arg(writeScope.others)
                    : ""
                readonly property string editorKind: typeof model.editorKind === "string" ? model.editorKind : "text"
                readonly property bool choicesTruncated: model.editorChoicesTruncated === true
                // fromTab: the editor before it moved on here (C2), which
                // opens it even while that editor's write still runs.
                function startEdit(fromTab) {
                    const may = fromTab === true ? editableRow && panel.provider.allowMutations !== false
                                                   && panel.provider.connected === true : writable
                    if (!may && isValue) {
                        panel.sayNotWritable(watchRow)
                        return
                    }
                    if (!may || editing || !cellLoader.item)
                        return
                    watchList.currentIndex = index
                    if (panel.selectable)
                        panel.rows.select(index)
                    panel.writeRefusalText = ""
                    panel.writeRefusalChanges = false
                    panel.writeRefusalLeft = false
                    cellLoader.item.begin()
                }
                // Closes the cell without writing.
                function finishEdit(restoreFocus = true) {
                    if (cellLoader.item)
                        cellLoader.item.reset()
                    if (restoreFocus) watchList.forceActiveFocus()
                }
                // A reading not yet made, and one a newer reading should have
                // replaced (family A6).
                readonly property bool quiet: status === "" || status === "Pending" || status === "Not read" || status === "NotRead"
                readonly property bool waiting: isValue && quiet
                readonly property bool stale: status === "Stale"
                // The form the row shows: its own, else the Watchlist's. A
                // node's own is the one its values share, "mixed" when not.
                readonly property string ownFormat: typeof model.format === "string" && model.format !== "mixed"
                                                    ? model.format : ""
                readonly property string formatName: ownFormat.length > 0 ? ownFormat : panel.format
                readonly property string shownText: typeof model.shownText === "string" && model.shownText.length > 0
                                                    ? model.shownText : text
                // A bool's or an enum's word and its code (WL-9 B2): two tones,
                // the code never cut.
                readonly property string valueWord: typeof model.valueWord === "string" ? model.valueWord : ""
                readonly property string valueCode: typeof model.valueCode === "string" ? model.valueCode : ""
                readonly property bool twoTone: isValue && valueWord.length > 0 && !missing
                // Numbers, bools and enums stand on the column's right edge in
                // every form; text stays on the left.
                readonly property bool leftValue: !isValue || (!twoTone
                    && !/^(?:[usi](?:8|16|32|64)|f(?:32|64)|bool|enum.*)$/.test(typeText)
                    && !/^[-+]?(?:\d|\.\d|0[xX]|NaN|inf)/.test(shownText))
                // A word only for a problem or a stale value; waiting is "–" alone,
                // its word in the value's tooltip and the row's description.
                // A value inside a Field shares its Field's word, but one its
                // device lacks says so itself.
                readonly property string statusText: ok || quiet || (nested && !missing) || isGroup ? "" : panel.statusWord(status)
                readonly property string waitingText: waiting ? panel.statusWord(status === "" ? "Not read" : status) : ""
                // rw on every row of a Field, its members and elements included,
                // in the Field's own colour (WL-9 A1). Groups have none.
                readonly property string accessText: !isGroup && typeof model.access === "string" ? model.access : ""
                readonly property string accessWords: accessText === "rw" ? qsTr("read and write")
                                                    : accessText === "r-" ? qsTr("read only") : ""
                readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                // A text held in the row (WL-9 C4): the provider's, or the
                // panel's own for a provider that keeps none.
                readonly property var localHeld: panel.localHeld[watchRow.name] !== undefined ? panel.localHeld[watchRow.name] : null
                readonly property string heldText: typeof model.heldText === "string" && model.heldText.length > 0 ? model.heldText
                                                 : localHeld ? localHeld.text : ""
                readonly property string heldReason: typeof model.heldReason === "string" && model.heldReason.length > 0 ? model.heldReason
                                                   : localHeld ? localHeld.reason : ""
                readonly property string heldKind: typeof model.heldKind === "string" && model.heldKind.length > 0 ? model.heldKind
                                                 : localHeld ? "invalid" : ""
                // The row's write (WL-9 C5): under way, read back otherwise,
                // not read back.
                readonly property string writeState: typeof model.writeState === "string" ? model.writeState : ""
                readonly property string writtenText: typeof model.writtenText === "string" ? model.writtenText : ""
                readonly property bool pendingWrite: isValue && writeState === "pending"
                readonly property bool differs: isValue && writeState === "differs"
                readonly property bool unconfirmed: isValue && writeState === "unconfirmed" && writtenText.length > 0
                readonly property bool isHeld: isValue && heldText.length > 0 && !pendingWrite
                // What the device holds, after the held text's reason: the row's
                // reading in its format; while the one read runs, that; with no
                // reading, nothing.
                readonly property string holdsTail: !isHeld ? ""
                    : panel.provider.watchActive !== true && panel.provider.watchPending === true ? qsTr("reading the device…")
                    : ok && shownText.length > 0 ? qsTr("device holds %1").arg(shownText) : ""
                readonly property string heldLine: holdsTail.length === 0 ? heldReason
                    : qsTr("%1 · %2").arg(heldReason.replace(/\.$/, "")).arg(holdsTail)
                // A read back as written flashes the cell once: the user's.
                onWriteStateChanged: if (writeState === "confirmed" && cellLoader.item) cellLoader.item.flash()
                // The change mark (WL-9 E5): a value whose reading changed, 1 s;
                // a folded node or group for the values under it. Not on the
                // row edited, a held row, a stale one, or an open group.
                readonly property double changedAt: typeof model.changedAt === "number" ? model.changedAt : 0
                readonly property var changedBelow: model.changedBelow !== undefined && model.changedBelow !== null ? model.changedBelow : []
                onChangedAtChanged: if (changedAt > 0) panel.noteChange(changedAt)
                onChangedBelowChanged: {
                    let last = 0
                    for (let i = 0; i < changedBelow.length; ++i)
                        last = Math.max(last, changedBelow[i])
                    if (last > 0)
                        panel.noteChange(last)
                }
                readonly property int changingBelow: {
                    void panel.markClock
                    const now = Date.now()
                    let n = 0
                    for (let i = 0; i < changedBelow.length; ++i)
                        if (now - changedBelow[i] < 1000)
                            ++n
                    return n
                }
                // The cell's editor is open: told by the cell, so the mark
                // never makes the cell while it is worked out.
                property bool cellOpen: false
                readonly property bool changeMarked: {
                    void panel.markClock
                    if (cellOpen || isHeld || stale)
                        return false
                    if (isValue)
                        return changedAt > 0 && Date.now() - changedAt < 1000
                    return !expanded && changingBelow > 0
                }
                // Reports this row's widths again when the panel forgot them.
                readonly property int measureEpoch: panel.measureEpoch
                // After the change has settled (a new zoom's fonts); the timer
                // is the row's own, so a row destroyed meanwhile is not called.
                onMeasureEpochChanged: reportAgain.restart()
                Timer { id: reportAgain; interval: 0; onTriggered: watchRow.report() }
                // Numbers never elide; text, words and summaries do, within the cap.
                function reportValue() {
                    // A node's summary uses the name's room, never the column's.
                    if (!watchRow.isValue)
                        return
                    const width = Math.ceil(valueMetrics.advanceWidth)
                    if (watchRow.leftValue || watchRow.twoTone)
                        panel.noteMeasured(width, 0, 0, false)
                    else
                        panel.noteNumber(width)
                }
                function reportName() {
                    panel.noteName(nameCell.nameLeft + nameCell.naturalWidth, watchRow.depth)
                }
                function report() {
                    watchRow.reportValue()
                    watchRow.reportName()
                    panel.noteMeasured(0,
                                       typeMetrics.text.length > 0 ? typeMetrics.advanceWidth + Type.tight : 0,
                                       statusMetrics.text.length > 0 ? statusMetrics.advanceWidth + Type.tight : 0,
                                       accessText.length > 0)
                    panel.noteShortType(typeShortMetrics.text.length > 0 ? typeShortMetrics.advanceWidth + Type.tight : 0)
                }
                readonly property bool ok: status === "Ok"
                // Waiting is not a problem; Stale and failures are.
                // A path this device lacks: said in a quiet word, never read or written.
                readonly property bool missing: status === "Missing" || status === "Unwatchable"
                // No device holds the row (WL-4c): its last value, stale, is
                // the panel's one note, not a warning on every row.
                readonly property bool unbound: Number(model.watchKey) >= 4503599627370496  // 2^52
                readonly property bool problem: !ok && !quiet && !missing && !unbound
                // The sentence a status word stands for, where there is one.
                readonly property string statusDetail: status === "Missing"
                    ? qsTr("%1 is not in this device's descriptor. Kept; read on any device that has it.").arg(name)
                    : status === "Unwatchable" && typeof model.writeRefusal === "string" ? model.writeRefusal : ""
                readonly property bool current: ListView.isCurrentItem && watchList.activeFocus
                // In the selection (the current row stands for it where the
                // rows keep none); drawn strong while the list has the keys,
                // quiet without them, so it stays visible.
                readonly property bool marked: panel.selectable ? model.selected === true
                                                                : ListView.isCurrentItem && watchList.activeFocus
                readonly property bool active: marked && (watchList.activeFocus || rowMenu.visible)
                // Muted text steps up on the quiet fill, to stay readable.
                readonly property color muted: marked && !active ? Theme.textSecondary : Theme.textMuted
                readonly property bool removeShown: !editing && (rowHover.hovered || current)
                readonly property color foreground: active ? Theme.selectedForeground : Theme.textPrimary
                // The value's colour: the change mark's info, a reading's
                // primary, else muted; the selection's foreground on a focused one.
                readonly property color valueInk: active ? Theme.selectedForeground
                                                : changeMarked ? Theme.info : ok ? Theme.textPrimary : muted
                // One line, plus the cell's caption while it has something to say.
                readonly property real valueHeight: cellLoader.visible && cellLoader.item
                    ? Math.max(Type.rowHeight, cellLoader.item.implicitHeight) : Type.rowHeight
                // Where this row's columns stand: the panel's, but a name that
                // would drop below its floor takes room from its own type, which
                // gives way at its right (WL-9 A2 step 4).
                readonly property real nameNeed: Math.min(nameCell.naturalWidth, Type.nameFloor)
                readonly property real deficit: isGroup || panel.typeColumn <= 0 ? 0
                    : Math.max(0, Math.min(nameNeed - (panel.valueX - panel.columnGap - nameCell.nameLeft),
                                           panel.typeColumn - panel.typeLeast))
                readonly property real valueLeft: panel.valueX + deficit
                readonly property real typeLeft: panel.typeX + deficit
                // What a keyboard or screen reader user hears: the hidden
                // columns too, and the status the glyph only marks (spec F).
                Accessible.role: panel.treeView ? Accessible.TreeItem : Accessible.ListItem
                Accessible.selectable: panel.selectable
                Accessible.selected: watchRow.marked
                Accessible.expandable: watchRow.branch
                Accessible.expanded: watchRow.branch && watchRow.expanded
                // What a nested row belongs to: "Config", "Phase[2]" (WL-2c).
                readonly property string parentLabel: typeof model.parentLabel === "string" ? model.parentLabel : ""
                // The List's path is heard leaf first: "Gain, in device, Config".
                Accessible.name: [panel.treeView ? watchRow.name.substring(nameCell.leafStart) : pathLabel.spokenName,
                                  watchRow.parentLabel.length === 0 ? ""
                                  : watchRow.name.charAt(nameCell.leafStart) === "["
                                    ? qsTr("element of %1").arg(watchRow.parentLabel)
                                    : qsTr("member of %1").arg(watchRow.parentLabel),
                                  watchRow.isGroup ? qsTr("group, %L1 watched").arg(watchRow.leaves)
                                  : watchRow.isValue ? ""
                                  : watchRow.node === "array" ? qsTr("array, %L1 watched").arg(watchRow.leaves)
                                  : qsTr("struct, %L1 watched").arg(watchRow.leaves),
                                  watchRow.shownText.length > 0 ? watchRow.shownText : watchRow.isValue ? "–" : "",
                                  watchRow.typeText,
                                  watchRow.accessWords]
                    .filter(part => part.length > 0).join(", ")
                Accessible.description: [watchRow.isHeld ? (watchRow.heldKind === "refused"
                                             ? qsTr("refused value %1, %2").arg(watchRow.heldText).arg(watchRow.heldLine.replace(/^Refused: /, "").split(" \u00B7 ").join(", "))
                                             : qsTr("value not written %1, %2").arg(watchRow.heldText).arg(watchRow.heldLine.replace(/^Not written: /, "").split(" \u00B7 ").join(", ")))
                                         : "",
                                         watchRow.differs ? qsTr("you wrote %1; the device reads back %2").arg(watchRow.writtenText).arg(watchRow.shownText) : "",
                                         watchRow.changeMarked ? qsTr("changed") : "",
                                         watchRow.statusText.length > 0 ? watchRow.statusText : watchRow.waitingText,
                                         watchRow.statusDetail,
                                         watchRow.isValue && watchRow.ownFormat.length > 0
                                             ? qsTr("shown in %1").arg(panel.formatWord(watchRow.ownFormat)) : ""]
                    .filter(part => part.length > 0).join(", ")
                height: valueHeight
                // The row's fill spans the pane (WL-9 E1): stripes, hover and
                // the selection bleed to both edges; a row with line 2 is one
                // stripe over both lines.
                Rectangle {
                    objectName: "watchRowFill"
                    anchors.fill: parent
                    color: watchRow.active ? Theme.stateSelected
                         : watchRow.marked ? Theme.stateSelectedInactive
                         : rowHover.hovered ? Theme.stateHover
                         : watchRow.index % 2 ? Theme.surfaceAlternate : "transparent"
                }
                // Indent guides (the kinds design, E): one 1 px line under each
                // ancestor's chevron, through line 2 too; the current branch's
                // brighter.
                Repeater {
                    model: panel.treeView ? watchRow.depth : 0
                    Rectangle {
                        required property int index
                        objectName: "indentGuide"
                        x: Math.round(Type.padding + index * Type.indent + Type.indent / 2)
                        width: 1
                        height: watchRow.height
                        readonly property bool active: index === panel.guideLevel
                            && watchRow.index >= panel.guideFrom && watchRow.index <= panel.guideTo
                        color: active ? Theme.outline : Theme.outlineVariant
                        Accessible.ignored: true
                    }
                }
                // The change mark's line at the row's left edge, its full height.
                Rectangle {
                    objectName: "watchChangeMark"
                    visible: watchRow.changeMarked
                    x: 0
                    width: 2
                    height: watchRow.height
                    color: Theme.info
                    Accessible.ignored: true
                }
                // The keyboard's row while the list has the keys: a frame inside
                // the row round both lines, the list clipping anything outside it.
                Rectangle {
                    objectName: "watchRowFocusFrame"
                    visible: watchRow.current && !watchRow.editing
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Type.radius
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.focusRing
                    z: 1
                }
                HoverHandler { id: rowHover }
                TapHandler {
                    id: rowTap
                    // A click selects the row alone, Ctrl+click toggles it,
                    // Shift+click takes the rows from the anchor.
                    onTapped: {
                        const modifiers = rowTap.point.modifiers
                        watchList.currentIndex = watchRow.index
                        if (panel.selectable) {
                            if (modifiers & Qt.ShiftModifier)
                                panel.rows.selectRange(watchRow.index, (modifiers & Qt.ControlModifier) !== 0)
                            else if (modifiers & Qt.ControlModifier)
                                panel.rows.select(watchRow.index, true)
                            else
                                panel.rows.select(watchRow.index)
                            panel.selectionGesture()
                        }
                        if (!watchRow.editing)
                            watchList.forceActiveFocus(Qt.MouseFocusReason)
                    }
                    onDoubleTapped: if (rowTap.point.modifiers === Qt.NoModifier && watchRow.branch)
                                        panel.rows.setExpanded(watchRow.index, !watchRow.expanded)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        watchList.currentIndex = watchRow.index
                        // Inside the selection the menu is the selection's.
                        if (panel.selectable && !watchRow.marked)
                            panel.rows.select(watchRow.index)
                        panel.openRowMenu(watchRow.index, watchRow, eventPoint.position.x, eventPoint.position.y)
                    }
                }
                // A folded node's values in brief, in the name's spare room up
                // to the numbers' edge, so no column moves when a row folds; a
                // value held under it puts the error glyph before it.
                ErrorGlyph {
                    id: summaryGlyph
                    objectName: "watchRowSummaryGlyph"
                    readonly property string held: typeof watchRow.model.heldBelow === "string" ? watchRow.model.heldBelow : ""
                    visible: summaryText.visible && held.length > 0
                    x: summaryText.x + summaryText.width - summaryText.contentWidth - Type.spacing - width
                    y: (Type.rowHeight - height) / 2
                    HoverHandler { id: summaryGlyphHover }
                    PlainToolTip { visible: summaryGlyphHover.hovered; text: summaryGlyph.held; delay: 700 }
                }
                Text {
                    id: summaryText
                    objectName: "watchRowSummary"
                    visible: !watchRow.isValue && !watchRow.isGroup && watchRow.text.length > 0 && width > 0
                    readonly property real start: nameCell.x + nameCell.leafEnd + panel.columnGap
                                                  + (summaryGlyph.held.length > 0 ? summaryGlyph.width + Type.spacing : 0)
                    x: start
                    y: 0
                    width: Math.max(0, watchRow.valueLeft + panel.valueColumn - start)
                    height: Type.rowHeight
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    textFormat: Text.PlainText
                    text: watchRow.text
                    font.family: Type.fixedFamily
                    font.pointSize: panel.font.pointSize
                    font.italic: watchRow.stale || watchRow.problem
                    color: watchRow.active ? Theme.selectedForeground
                         : watchRow.ok ? Theme.textPrimary : watchRow.muted
                    elide: Text.ElideRight
                    HoverHandler { id: summaryHover }
                    PlainToolTip {
                        visible: summaryHover.hovered && (summaryText.truncated || summaryGlyph.held.length > 0)
                        text: summaryGlyph.held.length > 0 ? watchRow.text + "\n" + summaryGlyph.held : watchRow.text
                        delay: 700
                    }
                }
                // The tree's chevron, an indent wide at the row's depth; the
                // indent is kept on values, so names of one depth share an x.
                Item {
                    objectName: "watchRowChevron"
                    visible: panel.treeView && watchRow.branch
                    x: Type.padding + watchRow.depth * Type.indent
                    y: 0
                    width: Type.indent
                    height: Type.rowHeight
                    Accessible.ignored: true
                    Chevron {
                        anchors.centerIn: parent
                        width: Type.iconSize
                        height: Type.iconSize
                        open: watchRow.expanded
                        color: watchRow.active ? Theme.selectedForeground : Theme.textSecondary
                    }
                    TapHandler {
                        onTapped: {
                            watchList.currentIndex = watchRow.index
                            panel.rows.setExpanded(watchRow.index, !watchRow.expanded)
                        }
                    }
                }
                // What the row is, in its own slot after the chevron's (the
                // kinds design): folder, {}, [] or a ring. The list has none:
                // every row there is a value.
                KindIcon {
                    visible: panel.treeView
                    x: Type.padding + (watchRow.depth + 1) * Type.indent
                    y: (Type.rowHeight - height) / 2
                    kind: watchRow.node
                    color: watchRow.active ? Theme.selectedForeground : Theme.textSecondary
                }
                // The List's name is the one path label (the review's 14.3):
                // prefix muted, leaf bright, whole segments dropping under one
                // "…" and the leaf eliding last (metrology/…/phaseA/H3). The
                // tree's is the row's own step, eliding in its middle.
                Item {
                    id: nameCell
                    objectName: "watchRowName"
                    readonly property real nameLeft: panel.nameXAt(watchRow.depth)
                    readonly property real naturalWidth: panel.treeView ? Math.ceil(leafMetrics.advanceWidth) : pathLabel.implicitWidth
                    // Where the name's text ends in the cell.
                    readonly property real leafEnd: panel.treeView ? leafText.x + leafText.width : pathLabel.usedWidth
                    x: nameLeft
                    y: 0
                    width: Math.max(1, (watchRow.isGroup ? groupCount.x - panel.columnGap : watchRow.valueLeft - panel.columnGap) - nameLeft)
                    height: Type.rowHeight
                    readonly property int leafStart: typeof watchRow.model.leafStart === "number"
                                                     ? Math.max(0, Math.min(watchRow.name.length, watchRow.model.leafStart)) : 0
                    onNaturalWidthChanged: watchRow.reportName()
                    Component.onCompleted: watchRow.reportName()
                    TextMetrics {
                        id: leafMetrics
                        font.family: panel.font.family
                        font.pointSize: panel.font.pointSize
                        font.weight: leafText.font.weight
                        text: leafText.text
                    }
                    PathLabel {
                        id: pathLabel
                        objectName: "watchRowPath"
                        visible: !panel.treeView
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: parent.height
                        path: panel.treeView ? "" : watchRow.name
                        font: panel.font
                        prefixColor: watchRow.active ? Theme.selectedForeground : watchRow.muted
                        leafColor: watchRow.foreground
                        prefixObjectName: "watchRowPrefix"
                        leafObjectName: "watchRowLeaf"
                        toolTipEnabled: false
                    }
                    Text {
                        id: leafText
                        objectName: "watchRowLeaf"
                        visible: panel.treeView
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width)
                        textFormat: Text.PlainText
                        // A step escapes a "/" or "." of its name; the tree shows the name.
                        text: watchRow.name.substring(nameCell.leafStart).replace(/\\(.)/g, "$1")
                        font.family: panel.font.family
                        font.pointSize: panel.font.pointSize
                        // DemiBold only on groups (the kinds design).
                        font.weight: watchRow.isGroup ? Type.headingWeight : Font.Normal
                        color: watchRow.foreground
                        elide: Text.ElideMiddle
                    }
                    // Edited while polling (WL-5): the live reading beside the
                    // name, "Now 1.48"; a failing read says its status.
                    Text {
                        id: nowText
                        objectName: "watchRowNow"
                        x: nameCell.leafEnd + Type.padding
                        anchors.verticalCenter: parent.verticalCenter
                        // Only the room the editor leaves; none, and its words go
                        // into the editor's description.
                        readonly property real room: cellLoader.item
                            ? cellLoader.x + cellLoader.width - cellLoader.item.editorWidth - nameCell.x - nameCell.leafEnd - 2 * Type.padding
                            : 0
                        width: Math.max(0, Math.min(parent.width - nameCell.leafEnd - Type.padding, room))
                        visible: watchRow.editing && watchRow.isValue && panel.provider.watchActive === true && width > Type.bodyPixels * 3
                        textFormat: Text.PlainText
                        text: watchRow.ok ? qsTr("Now %1").arg(watchRow.shownText) : watchRow.status
                        font.family: Type.fixedFamily
                        font.pointSize: Type.caption
                        color: watchRow.ok ? (watchRow.active ? Theme.selectedForeground : watchRow.muted) : Theme.warning
                        elide: Text.ElideRight
                        Accessible.name: text
                    }
                    HoverHandler { id: nameHover }
                    PlainToolTip {
                        objectName: "watchRowNameTip"
                        visible: nameHover.hovered && (!watchRow.isValue || (panel.treeView ? leafText.truncated : pathLabel.elided))
                        // The whole path, broken after each "/".
                        text: watchRow.isValue ? watchRow.name.replace(/\//g, "/​")
                            : [watchRow.name, watchRow.typeText,
                               watchRow.leaves === 1 ? qsTr("1 value watched") : qsTr("%Ln values watched", "", watchRow.leaves)]
                              .filter(part => part.length > 0).join(" · ")
                        delay: 700
                    }
                }
                // A group shows how many values it holds, right-aligned on the
                // column group's right edge; folded and changing, how many move.
                Text {
                    id: groupCount
                    objectName: "watchGroupCount"
                    visible: watchRow.isGroup
                    x: panel.groupRight - width
                    y: 0
                    height: Type.rowHeight
                    width: implicitWidth
                    verticalAlignment: Text.AlignVCenter
                    textFormat: Text.StyledText
                    readonly property string count: qsTr("%L1 watched").arg(watchRow.leaves)
                    readonly property string moving: watchRow.changingBelow > 0 && !watchRow.expanded
                                                     ? qsTr("%L1 changing").arg(watchRow.changingBelow) : ""
                    // The tail in the info colour, on a focused selection the
                    // selection's foreground.
                    text: moving.length === 0 ? count
                        : count + " · <font color=\"" + (watchRow.active ? Theme.selectedForeground : Theme.info) + "\">" + moving + "</font>"
                    font.pointSize: Type.caption
                    color: watchRow.active ? Theme.selectedForeground : watchRow.muted
                    Accessible.name: moving.length === 0 ? count : count + ", " + moving
                }
                // The value: a number or a text as it shows in the row's form, or
                // a bool's or an enum's word with its code (WL-9 B2); a write the
                // device read back otherwise has the warning glyph at its left;
                // one not read back shows what was typed, italic, until a reading.
                Item {
                    id: valueBox
                    objectName: "watchValueBox"
                    visible: watchRow.isValue && !watchRow.cellShown
                    x: watchRow.valueLeft
                    y: 0
                    width: panel.valueColumn
                    height: Type.rowHeight
                    readonly property string plain: watchRow.missing ? "–"
                        : watchRow.unconfirmed ? watchRow.writtenText
                        : watchRow.shownText.length > 0 ? watchRow.shownText : watchRow.waiting ? "–" : ""
                    TextMetrics {
                        id: valueMetrics
                        font.family: Type.fixedFamily
                        font.pointSize: panel.font.pointSize
                        text: watchRow.shownText
                        onAdvanceWidthChanged: watchRow.reportValue()
                        Component.onCompleted: watchRow.reportValue()
                    }
                    Label {
                        id: valueText
                        objectName: "watchValueText"
                        visible: !watchRow.twoTone || watchRow.unconfirmed
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        // Not read yet: a dash at the value's alignment, no word.
                        text: valueBox.plain
                        font.family: Type.fixedFamily
                        font.italic: watchRow.unconfirmed || ((watchRow.stale || watchRow.problem) && watchRow.text.length > 0)
                        color: watchRow.unconfirmed && !watchRow.active ? Theme.textSecondary : watchRow.valueInk
                        horizontalAlignment: watchRow.leftValue ? Text.AlignLeft : Text.AlignRight
                        leftPadding: watchRow.leftValue ? panel.pencilRoom : 0
                        elide: Text.ElideRight
                    }
                    Row {
                        id: twoToneRow
                        objectName: "watchValueTwoTone"
                        visible: watchRow.twoTone && !watchRow.unconfirmed
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            id: wordText
                            objectName: "watchValueWord"
                            width: Math.max(0, Math.min(implicitWidth, valueBox.width - codeText.implicitWidth))
                            textFormat: Text.PlainText
                            text: watchRow.valueWord
                            elide: Text.ElideRight
                            font.family: Type.fixedFamily
                            font.italic: watchRow.stale || watchRow.problem
                            color: watchRow.valueInk
                        }
                        Text {
                            id: codeText
                            objectName: "watchValueCode"
                            visible: watchRow.valueCode.length > 0
                            textFormat: Text.PlainText
                            text: " (" + watchRow.valueCode + ")"
                            font.family: Type.fixedFamily
                            font.italic: watchRow.stale || watchRow.problem
                            color: watchRow.active ? Theme.selectedForeground
                                 : watchRow.changeMarked ? Theme.info : Theme.textSecondary
                        }
                    }
                    ToolIcon {
                        id: readBackGlyph
                        objectName: "watchReadBackGlyph"
                        visible: watchRow.differs
                        width: panel.markSize
                        height: panel.markSize
                        readonly property real textStart: valueBox.width - (watchRow.twoTone ? twoToneRow.width : Math.min(valueText.contentWidth, valueBox.width))
                        x: textStart - Type.spacing - width
                        y: (Type.rowHeight - height) / 2
                        name: "warning"
                        color: Theme.warning
                        Accessible.ignored: true
                    }
                    HoverHandler { id: valueHover }
                    PlainToolTip {
                        visible: valueHover.hovered && text.length > 0
                        delay: 700
                        text: watchRow.differs ? qsTr("You wrote %1; the device reads back %2.").arg(watchRow.writtenText).arg(watchRow.shownText)
                            : watchRow.unconfirmed ? qsTr("You wrote %1; it was not read back.").arg(watchRow.writtenText)
                            : watchRow.waitingText.length > 0 ? watchRow.waitingText
                            : (valueText.visible && valueText.truncated) || (twoToneRow.visible && wordText.truncated)
                              ? (watchRow.ownFormat.length === 0 ? watchRow.shownText
                                 : qsTr("%1 · shown in %2").arg(watchRow.shownText).arg(panel.formatWord(watchRow.ownFormat)))
                            : watchRow.ownFormat.length === 0 ? ""
                            : watchRow.ownFormat === panel.format
                              ? qsTr("%1 · shown in %2").arg(watchRow.shownText).arg(panel.formatWord(watchRow.ownFormat))
                            : qsTr("%1 · shown in %2 (the Watchlist shows %3)").arg(watchRow.shownText)
                                  .arg(panel.formatWord(watchRow.ownFormat)).arg(panel.formatWord(panel.format))
                    }
                }
                // A double-click on the value opens its editor (WL-9 C1), also
                // while the cell lies over the value - a held text, a write, a
                // flash - when the value box hides (WL-9b): the area reaches as
                // far left as the cell shows, its held text included.
                Item {
                    objectName: "watchValueTapArea"
                    readonly property real reach: cellLoader.visible && cellLoader.item
                        ? Math.max(panel.valueColumn, cellLoader.item.editorWidth) : panel.valueColumn
                    visible: watchRow.isValue
                    x: watchRow.valueLeft + panel.valueColumn - reach
                    y: 0
                    width: reach
                    height: Type.rowHeight
                    TapHandler {
                        id: valueTap
                        enabled: watchRow.writable || watchRow.writeRefusal.length > 0
                        onDoubleTapped: if (valueTap.point.modifiers === Qt.NoModifier) watchRow.startEdit()
                    }
                }
                // The editor is Details' own cell, in the Value column: it looks
                // and sizes the same, grows leftwards only as far as its text
                // needs, and keeps the row one line plus its line 2. It also
                // shows a held text, a write under way and a confirmed read-back's
                // flash.
                Loader {
                    id: cellLoader
                    objectName: "watchValueCellLoader"
                    active: watchRow.isValue && (watchRow.editableRow || watchRow.isHeld)
                    visible: item ? (item.open || item.refusal.length > 0 || watchRow.isHeld || watchRow.pendingWrite
                                     || item.flashing) : false
                    x: watchRow.valueLeft
                    y: 0
                    width: panel.valueColumn
                    sourceComponent: ValueCell {
                        objectName: "valueCell"
                        name: watchRow.name
                        value: watchRow.shownText.length > 0 ? watchRow.shownText : "–"
                        valueWord: watchRow.twoTone ? watchRow.valueWord : ""
                        valueCode: watchRow.twoTone ? watchRow.valueCode : ""
                        typeText: watchRow.typeText
                        editable: watchRow.editableRow
                        editorText: typeof watchRow.model.editorText === "string" ? watchRow.model.editorText
                            : watchRow.model.sampleExact === true
                                ? (watchRow.model.sampleValue === 0 && 1 / watchRow.model.sampleValue < 0 ? "-0" : String(watchRow.model.sampleValue))
                                : watchRow.text
                        editorKind: watchRow.editorKind
                        editorChoices: watchRow.model.editorChoices !== undefined ? watchRow.model.editorChoices : []
                        choicesTruncated: watchRow.choicesTruncated
                        hexCodes: watchRow.formatName === "hex"
                        highlighted: watchRow.active
                        changed: watchRow.changeMarked
                        heldText: watchRow.isHeld ? watchRow.heldText : ""
                        heldReason: watchRow.isHeld ? watchRow.heldLine : ""
                        heldHint: qsTr("F2 to change it · Esc drops it")
                        pendingText: watchRow.pendingWrite ? watchRow.writtenText : ""
                        reducedMotion: panel.reducedMotion
                        openOnClick: false
                        listOpensOnBegin: false
                        editorRoom: Math.max(0, watchRow.valueLeft - nameCell.nameLeft
                                                - Math.min(panel.treeView ? leafText.implicitWidth : pathLabel.leafImplicitWidth,
                                                           Type.bodyPixels * 3) - Type.tight)
                        captionRoom: Math.max(0, watchRow.valueLeft - nameCell.nameLeft)
                        captionRightRoom: Math.max(0, panel.groupRight - watchRow.valueLeft - panel.valueColumn)
                        note: watchRow.scopeNote
                        noteDetail: watchRow.scopeDetail
                        extraDescription: [watchRow.editing && !nowText.visible && nowText.text.length > 0 ? nowText.text : "",
                                           qsTr("Enter writes; Escape cancels.")].filter(part => part.length > 0).join(" ")
                        // Typing only checks; Enter, Tab, a click away or a choice writes.
                        checkText: function(text) {
                            const result = typeof panel.provider.watchEditPreview === "function"
                                ? panel.provider.watchEditPreview(watchRow.sourceRow, text) : null
                            const error = result && result.error ? result.error : ""
                            return { ok: error.length === 0 && text.length > 0,
                                     error: error.length === 0 && text.length === 0 ? qsTr("Enter a value.") : error,
                                     preview: !error && result && result.preview ? result.preview : "" }
                        }
                        commitText: function(text) { panel.writeRow(watchRow, text) }
                        onTextHeld: function(text, reason) { panel.holdRowText(watchRow, text, reason) }
                        onOpenChanged: watchRow.cellOpen = open
                        onTabbed: function(forward) { panel.openNextWritable(watchRow.index, forward) }
                        onFinished: function(byKey) { if (byKey) watchList.forceActiveFocus() }
                    }
                }
                // A named type in two tones, its scope muted; "…::" while
                // the column gives its scopes way (the review's 14.2).
                TypeLabel {
                    id: typeLabel
                    objectName: "watchRowType"
                    visible: !watchRow.isGroup && panel.typeColumn > 0
                    x: watchRow.typeLeft
                    y: 0
                    width: panel.typeColumn - watchRow.deficit
                    height: Type.rowHeight
                    verticalAlignment: Text.AlignVCenter
                    typeText: watchRow.typeText
                    shortScopes: panel.typeScopesCut
                    nameColor: watchRow.active ? Theme.selectedForeground : Theme.textSecondary
                    scopeColor: watchRow.active ? Theme.selectedForeground
                        : watchRow.marked ? Theme.textSecondary : Theme.textMuted
                    HoverHandler { id: typeHover }
                    PlainToolTip {
                        objectName: "watchRowTypeToolTip"
                        visible: typeHover.hovered && (typeLabel.truncated || typeLabel.cutShown)
                        text: watchRow.typeText
                        delay: 700
                    }
                }
                // rw: each letter, a missing right as "-" in the placeholder colour.
                Row {
                    objectName: "watchRowAccess"
                    visible: !watchRow.isGroup && panel.accessColumn > 0 && watchRow.accessText.length > 0
                    x: panel.accessX
                    y: 0
                    width: panel.accessColumn
                    height: Type.rowHeight
                    readonly property string text: watchRow.accessText
                    Repeater {
                        model: watchRow.accessText.split("")
                        Text {
                            required property string modelData
                            height: Type.rowHeight
                            verticalAlignment: Text.AlignVCenter
                            text: modelData
                            font.family: Type.fixedFamily
                            font.pointSize: Type.caption
                            color: watchRow.active ? Theme.selectedForeground
                                 : modelData === "-" && !watchRow.marked ? Theme.textDisabled : Theme.textSecondary
                        }
                    }
                    HoverHandler { id: accessHover }
                    PlainToolTip {
                        visible: accessHover.hovered && watchRow.accessText.length > 0
                        // A member is written with its whole Field.
                        text: watchRow.nested && watchRow.writeScope && watchRow.accessText === "rw"
                              ? qsTr("%1 · written with all of %2").arg(watchRow.accessWords).arg(watchRow.tail(watchRow.writeScope.field))
                              : watchRow.accessWords
                        delay: 700
                    }
                }
                // A reading's word only when it is not a fresh value: a
                // problem in warning, waiting in muted; the provider's
                // own words in the tooltip.
                Label {
                    id: statusLabel
                    objectName: "watchRowStatus"
                    visible: !watchRow.isGroup && panel.statusColumn > 0 && !panel.statusGlyph
                    x: panel.statusX
                    y: 0
                    width: panel.statusColumn
                    height: Type.rowHeight
                    verticalAlignment: Text.AlignVCenter
                    textFormat: Text.PlainText
                    text: watchRow.statusText
                    color: watchRow.active ? Theme.selectedForeground
                         : watchRow.problem ? Theme.warning
                         : watchRow.missing ? Theme.textSecondary : watchRow.muted
                    font.pointSize: Type.caption
                    elide: Text.ElideRight
                    HoverHandler { id: statusRowHover }
                    PlainToolTip {
                        visible: statusRowHover.hovered && statusLabel.text.length > 0
                                 && (statusLabel.truncated || statusLabel.text !== watchRow.status || watchRow.statusDetail.length > 0)
                        text: watchRow.statusDetail.length > 0 ? watchRow.statusDetail : watchRow.status
                        delay: 700
                    }
                }
                // Too little room: the word becomes a mark, the word in its tooltip.
                Item {
                    objectName: "watchRowStatusGlyph"
                    visible: !watchRow.isGroup && panel.statusGlyph
                    x: panel.statusX
                    y: 0
                    width: panel.statusColumn
                    height: Type.rowHeight
                    Text {
                        anchors.centerIn: parent
                        visible: watchRow.statusText.length > 0
                        text: watchRow.problem ? "!" : "·"
                        font.bold: true
                        color: watchRow.active ? Theme.selectedForeground
                             : watchRow.problem ? Theme.warning : Theme.textSecondary
                    }
                    HoverHandler { id: glyphHover }
                    PlainToolTip {
                        visible: glyphHover.hovered && watchRow.statusText.length > 0
                        text: watchRow.statusDetail.length > 0 ? watchRow.statusText + "\n" + watchRow.statusDetail
                                                               : watchRow.statusText
                        delay: 700
                    }
                }
                // Measure the type and the word for their columns, shown or
                // not: a hidden Text does not lay itself out, so a column
                // measured from its own label would never appear.
                TextMetrics {
                    id: typeMetrics
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    text: watchRow.typeText
                    onAdvanceWidthChanged: panel.noteMeasured(0, text.length > 0 ? advanceWidth + Type.tight : 0, 0,
                                                              watchRow.accessText.length > 0)
                    Component.onCompleted: panel.noteMeasured(0, text.length > 0 ? advanceWidth + Type.tight : 0, 0,
                                                              watchRow.accessText.length > 0)
                }
                TextMetrics {
                    id: typeShortMetrics
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    text: typeLabel.cut(watchRow.typeText)
                    onAdvanceWidthChanged: panel.noteShortType(text.length > 0 ? advanceWidth + Type.tight : 0)
                    Component.onCompleted: panel.noteShortType(text.length > 0 ? advanceWidth + Type.tight : 0)
                }
                TextMetrics {
                    id: statusMetrics
                    font.family: panel.font.family
                    font.pointSize: Type.caption
                    text: watchRow.statusText
                    onAdvanceWidthChanged: panel.noteMeasured(0, 0, text.length > 0 ? advanceWidth + Type.tight : 0, false)
                }
                // × shows on the row under the pointer and on the current
                // row, one gap after the last column; the keyboard removes with
                // Delete.
                Item {
                    x: panel.removeX
                    y: 0
                    width: panel.removeColumnWidth
                    height: Type.rowHeight
                    Basic.ToolButton {
                        id: remove
                        objectName: "removeWatchButton"
                        anchors.fill: parent
                        visible: watchRow.removeShown
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        text: "×"
                        enabled: !panel.changeLocked
                        font.pointSize: Type.body
                        contentItem: Text {
                            text: remove.text
                            font: remove.font
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: !remove.enabled ? Theme.textDisabled
                                 : remove.hovered ? Theme.textPrimary : Theme.textSecondary
                        }
                        background: Rectangle {
                            // Inside the current row's focus frame.
                            anchors.fill: parent
                            anchors.margins: 3
                            radius: Type.radius
                            color: remove.down ? Theme.statePressed : remove.hovered ? Theme.stateHover : "transparent"
                        }
                        onClicked: panel.removeAt(watchRow.index, true)
                        readonly property string what: watchRow.isValue ? qsTr("Remove %1").arg(watchRow.name)
                            : watchRow.leaves === 1 ? qsTr("Remove the 1 value of %1").arg(watchRow.name)
                            : qsTr("Remove the %Ln values of %1", "", watchRow.leaves).arg(watchRow.name)
                        Accessible.name: what
                        ToolTip.visible: hovered
                        ToolTip.delay: 700
                        // Delete does the same only where this row is all that is selected.
                        readonly property string tip: watchRow.ListView.isCurrentItem && !panel.onSelection(watchRow.index)
                                                      ? qsTr("%1 (Delete)").arg(what) : what
                        ToolTip.text: tip
                    }
                }
            }
            // A focus frame for the list itself: the rows show which is current.
            // Both it and the empty line sit on the view, not on its content.
            Rectangle {
                parent: watchList
                anchors.fill: parent
                color: "transparent"
                border.width: watchList.activeFocus && watchList.currentIndex < 0 ? 2 : 0
                border.color: Theme.focusRing
            }
            Label {
                objectName: "watchEmpty"
                parent: watchList
                anchors.centerIn: parent
                width: parent.width - 2 * Type.padding
                visible: panel.open && panel.count === 0 && !panel.addAvailable
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: Theme.textMuted
                text: !panel.connected ? qsTr("Connect to a device to watch its Fields.")
                    : panel.treeBoxes ? qsTr("Tick Fields in the Device tree to watch them.")
                    : qsTr("Select a Field in the Device tree, then choose Watch the selected Field in the Watchlist's menu.")
            }
        }
        WatchAddRow {
            id: addRow
            visible: panel.open && panel.addAvailable
            Layout.fillWidth: true
            Layout.rightMargin: panel.scrollReserve
            Layout.minimumHeight: implicitHeight
            Layout.preferredHeight: implicitHeight
            provider: panel.provider
            onAdded: (notice, undoable, serial) => {
                panel.say(notice, serial)
                // The last value added, its parents opened, in view.
                const last = panel.count - 1
                const row = panel.rows ? panel.rows.reveal(last) : last
                if (row >= 0)
                    watchList.positionViewAtIndex(row, ListView.Contain)
            }
            onReveal: (row, notice) => {
                const shown = panel.rows ? panel.rows.reveal(row) : row
                if (shown >= 0)
                    panel.moveTo(shown, "select")
                panel.say(notice, 0)
            }
            onLeaveUp: {
                watchList.forceActiveFocus()
                panel.moveTo(watchList.count - 1, "select")
            }
        }
        // Under the "+" row: the room left, and while the list is empty what to do.
        // An open list asks for the room it always did, rows and "+" row included.
        Item {
            visible: panel.open && panel.addAvailable
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.max(0, Math.max(120, Type.rowHeight * 5)
                                                - watchList.Layout.preferredHeight - addRow.implicitHeight)
            Label {
                objectName: "watchEmptyAdd"
                visible: panel.count === 0
                x: Type.padding + Type.indent + Type.tight
                y: Type.tight
                width: parent.width - x - Type.padding
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: Theme.textMuted
                text: panel.connected ? qsTr("Tick Fields in the Device tree to watch them, or type a path.")
                                      : qsTr("Connect to a device to watch its Fields.")
            }
        }
    }

    // The two read modes: one is always chosen.
    ButtonGroup { id: modes; exclusive: true }
    // So are the two views, in the menu.
    ButtonGroup { id: viewItems; exclusive: true }

    // A row's menu (right-click, the Menu key or Shift+F10): its number
    // format, which a node gives every value under it, and removing it.
    Menu {
        id: rowMenu
        objectName: "watchRowMenu"
        readonly property var item: panel.menuRow >= 0 ? watchList.itemAtIndex(panel.menuRow) : null
        readonly property string own: item && typeof item.model.format === "string" ? item.model.format : ""
        // Opened inside a selection of several: the menu is the selection's.
        property int many: 0
        // The values the Plot item would take.
        property int plotCount: 0
        onAboutToShow: {
            many = panel.onSelection(panel.menuRow) ? panel.rows.selectedSourceRows().length : 0
            const values = panel.plotTraces && panel.rows ? panel.rows.plotValues(panel.menuRow) : []
            plotCount = values.filter(value => value.number).length
        }
        Menu {
            id: rowFormatMenu
            objectName: "watchRowFormatMenu"
            title: qsTr("Number format")
            enabled: panel.formatAvailable && typeof panel.provider.setWatchRowFormat === "function"
            ButtonGroup { id: rowFormatItems; exclusive: true }
            MenuItem {
                objectName: "watchRowFormat_inherit"
                text: qsTr("Like the Watchlist (%1)").arg(panel.formatWord(panel.format))
                checkable: true
                checked: rowMenu.many === 0 && rowMenu.own.length === 0
                ButtonGroup.group: rowFormatItems
                onTriggered: panel.formatRows(panel.menuRow, "")
            }
            Repeater {
                model: panel.formats
                MenuItem {
                    required property var modelData
                    objectName: "watchRowFormat_" + modelData.name
                    text: modelData.text
                    checkable: true
                    checked: rowMenu.many === 0 && rowMenu.own === modelData.name
                    ButtonGroup.group: rowFormatItems
                    Accessible.description: modelData.tip
                    onTriggered: panel.formatRows(panel.menuRow, modelData.name)
                }
            }
            // Where a row was given its form, every value taken back to
            // Original: the Watchlist's and every row's own.
            MenuSeparator {
                visible: panel.formatsResettable
                height: visible ? implicitHeight : 0
            }
            MenuItem {
                objectName: "watchRowFormatReset"
                visible: panel.formatsResettable
                height: visible ? implicitHeight : 0
                enabled: visible && rowFormatMenu.differ
                leftPadding: panel.checkColumn(rowFormatMenu.count > 0 ? rowFormatMenu.itemAt(0) : null, horizontalPadding)
                text: qsTr("Reset number formats to Original")
                onTriggered: panel.resetFormats()
            }
            property bool differ: false
            onAboutToShow: differ = panel.formatsDiffer()
        }
        // On the live Plot (LP-1), or off it; its readings are kept either way.
        // A selection or a node plots every value in it.
        MenuItem {
            id: plotItem
            objectName: "watchRowPlot"
            readonly property var traces: panel.plotTraces
            readonly property bool one: rowMenu.many === 0 && rowMenu.item !== null && rowMenu.item.isValue
            readonly property string path: one ? rowMenu.item.name : ""
            readonly property bool shown: traces !== null && path.length > 0 && traces.plotted.indexOf(path) >= 0
            visible: traces !== null
            height: visible ? implicitHeight : 0
            enabled: rowMenu.plotCount > 0
            checkable: one
            checked: shown
            text: (one ? qsTr("Plot") : qsTr("Plot %L1 values").arg(rowMenu.plotCount)) + "\t" + qsTr("Ctrl+P")
            Accessible.description: one && rowMenu.plotCount === 0 ? qsTr("%1 is text; only numbers, booleans and enums are plotted.").arg(path)
                                  : one ? qsTr("Shows the readings of %1 on the Plot.").arg(path)
                                  : rowMenu.plotCount > 0 ? qsTr("Shows their readings on the Plot, or takes them off when all are on it.")
                                                          : qsTr("Nothing here can be plotted.")
            onTriggered: {
                panel.togglePlot(panel.menuRow)
                checked = Qt.binding(() => plotItem.shown)
            }
        }
        MenuItem {
            objectName: "watchRowBatch"
            visible: panel.batchAvailable
            height: visible ? implicitHeight : 0
            text: qsTr("Add to Batch") + "\t" + qsTr("Ctrl+B")
            onTriggered: panel.batchRows(panel.menuRow)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "watchRowCopy"
            text: (rowMenu.many > 0 ? qsTr("Copy %Ln values", "", rowMenu.many) : qsTr("Copy")) + "\t" + qsTr("Ctrl+C")
            onTriggered: panel.copyRows(panel.menuRow, "lines")
        }
        MenuItem {
            objectName: "watchRowCopyValue"
            text: qsTr("Copy value")
            enabled: rowMenu.many === 0 && rowMenu.item !== null && rowMenu.item.isValue && rowMenu.item.shownText.length > 0
            onTriggered: panel.copyRows(panel.menuRow, "value")
        }
        MenuItem {
            objectName: "watchRowCopyPath"
            text: (rowMenu.many > 0 ? qsTr("Copy %Ln paths", "", rowMenu.many) : qsTr("Copy path")) + "\t" + qsTr("Alt+Shift+C")
            onTriggered: panel.copyRows(panel.menuRow, "paths")
        }
        MenuSeparator {}
        MenuItem {
            objectName: "watchRowRemove"
            text: (rowMenu.many === 0 ? qsTr("Remove") : qsTr("Remove %Ln values", "", rowMenu.many))
                  + "\t" + qsTr("Delete")
            enabled: !panel.changeLocked
            onTriggered: panel.removeAt(panel.menuRow)
        }
        // A held text goes, the row showing what the device holds (WL-9 C4).
        MenuItem {
            objectName: "watchRowDropHeld"
            readonly property bool shown: rowMenu.many === 0 && rowMenu.item !== null && rowMenu.item.isHeld === true
            visible: shown
            height: shown ? implicitHeight : 0
            enabled: shown
            text: (rowMenu.item && rowMenu.item.heldKind === "refused" ? qsTr("Drop the refused value")
                                                                         : qsTr("Drop the text not written"))
                  + "\t" + qsTr("Esc")
            onTriggered: panel.dropRowHeld(rowMenu.item)
        }
    }
    // Values dropped from the Device tree join the Watchlist (LP-1g3). Over
    // the list itself: a second child of the Pane would change its size.
    DropArea {
        id: watchDrop
        objectName: "watchlistDrop"
        parent: watchList
        anchors.fill: parent
        keys: ["application/x-telemetry-paths"]
        property int count: 0
        // What a drop from the Device tree would do (section 16): the
        // provider's plan, its words on the frame before the release.
        property var plan: null
        readonly property bool refused: !!plan && (plan.status === "refused" || plan.status === "empty")
        readonly property string words: plan ? (refused ? plan.refusal : plan.words)
            : count === 1 ? qsTr("Drop to add to the Watchlist") : qsTr("Drop to add %L1 to the Watchlist").arg(count)
        onEntered: (drag) => {
            let payload = null
            try { payload = JSON.parse(drag.getDataAsString("application/x-telemetry-paths")) } catch (e) {}
            if (!payload || payload.from === "watchlist") {
                drag.accepted = false
                return
            }
            // A refusal is explained over the frame, then the drop refused.
            if (Array.isArray(payload.items) && typeof panel.provider.dropPlan === "function") {
                plan = panel.provider.dropPlan(payload.items, "watchlist")
                return
            }
            if (typeof panel.provider.addWatchPaths !== "function") {
                drag.accepted = false
                return
            }
            count = (payload.paths || []).length
        }
        onExited: { count = 0; plan = null }
        onDropped: (drop) => {
            let payload = null
            try { payload = JSON.parse(drop.getDataAsString("application/x-telemetry-paths")) } catch (e) {}
            const refused = watchDrop.refused
            count = 0
            plan = null
            if (!refused && panel.dropPaths(payload))
                drop.acceptProposedAction()
        }
        Rectangle {
            objectName: "watchlistDropFrame"
            anchors.fill: parent
            visible: watchDrop.containsDrag
            color: "transparent"
            border.width: 2
            border.color: watchDrop.refused ? Theme.error : Theme.focusRing
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Type.padding
                spacing: Type.tight
                ErrorGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: watchDrop.refused
                }
                Label {
                    objectName: "watchlistDropText"
                    width: Math.min(implicitWidth, watchDrop.width - Type.padding * 4)
                    text: watchDrop.words
                    wrapMode: Text.Wrap
                    color: watchDrop.refused ? Theme.error : Theme.textPrimary
                    padding: Type.tight
                    background: Rectangle { color: Theme.surface; radius: Type.radius }
                }
            }
        }
    }
}
