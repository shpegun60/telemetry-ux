// The Watchlist: the watched values and their latest readings. One toolbar
// row - Read once, Start, the interval, List | Tree and a ⋯ menu - the status
// under it, then the column titles and the rows: one per value with its whole
// path, or a tree grouped by path, as Live Expressions shows them. The toolbar
// and the titles stay where they are; only the rows scroll. As the panel
// narrows, "Every" goes, then Read once and List | Tree move into ⋯.
// Presentation only: the caller owns the provider and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Templates as T

Pane {
    id: panel
    required property var provider
    // The provider's history (UR-1): where its changes go as steps.
    readonly property var history: provider && provider.history ? provider.history : null
    // Ctrl+Z takes back the history's last step wherever in the panel the
    // keys are - the list, a toolbar button, the ⋯ button after its menu - but
    // in a text field, which takes back its own typing first. Until UX-B
    // gives the window one router, the panel calls the history itself.
    Keys.onShortcutOverride: event => {
        if (event.key === Qt.Key_Z && event.modifiers === Qt.ControlModifier && history !== null && history.canUndo)
            event.accepted = true
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Z && event.modifiers === Qt.ControlModifier)
            event.accepted = panel.undo()
    }
    // The serial of the step the history last recorded: an action of this
    // panel's that recorded one offers its Undo with its notice.
    property int recorded: 0
    Connections {
        target: panel.history
        function onRecorded(serial) { panel.recorded = serial }
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
    readonly property var formats: [
        { name: "original", text: qsTr("Original"), tip: qsTr("Original: as the type means it (1.5, High, true)") },
        { name: "dec", text: qsTr("Dec"), tip: qsTr("Dec: integers and codes in decimal (2, 1)") },
        { name: "hex", text: qsTr("Hex"), tip: qsTr("Hex: the value's bits, floats included (0x3FC00000)") }
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
        recorded = 0
        if (sources.length === 0 || !provider.setWatchRowFormat(sources, name))
            return
        forgetMeasured()
        if (many)
            say(name.length === 0 ? qsTr("%Ln values shown like the Watchlist (%1)", "", sources.length).arg(formatWord(format))
                                  : qsTr("%Ln values shown in %1", "", sources.length).arg(formatWord(name)),
                recorded)
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
    // Removing many with no Undo to bring them back (a host that gave the
    // provider no history) asks first; the rows asked about wait here.
    property var removalAsked: []
    // The last thing done, in a line under the toolbar: "Removed
    // device.Config.Gain · Undo". Like the Device tree's, it goes once
    // anything changes the Watchlist, the tree or a format - here, in the
    // Device tree, or by a project loaded. Its Undo shows while the step it
    // speaks of is the one Undo would take.
    property string noticeText: ""
    // The step the notice speaks of, or 0.
    property int noticeSerial: 0
    readonly property int revision: typeof provider.watchRevision === "number" ? provider.watchRevision : -1
    property int noticeRevision: -1
    function say(text, serial) {
        noticeText = text
        noticeSerial = typeof serial === "number" ? serial : 0
        noticeRevision = revision
        // The notice is what is said, not the selection it left.
        selectionAnnounce.stop()
        announcedCount = selectedCount
        if (text.length > 0)
            panel.Accessible.announce(text)
    }
    onRevisionChanged: {
        if (noticeText.length > 0 && revision !== noticeRevision)
            say("", 0)
        if (removeDialog.visible) {
            removalAsked = []
            removeDialog.close()
            panel.Accessible.announce(qsTr("The Watchlist changed; nothing was removed"))
        }
    }
    readonly property bool canUndo: noticeSerial > 0 && history !== null && history.canUndo
                                    && history.undoSerial === noticeSerial
    // Stops watching what row shows: a value, or every value under a node, as
    // one change where the provider takes several, said with Undo.
    function removeAt(row, confirmed, own) {
        const many = own !== true && onSelection(row)
        const sources = many ? sourcesAt(row) : rows ? rows.sourceRows(row) : [row]
        if (sources.length === 0 || changeLocked)
            return
        // A selection is named by its count: its rows may be out of view.
        const item = many ? null : watchList.itemAtIndex(row)
        const label = item ? item.name : ""
        const oneByOne = typeof provider.removeWatchRows !== "function"
        if (sources.length > 1 && confirmed !== true && (oneByOne || history === null)) {
            removalAsked = sources
            removeDialog.open()
            return
        }
        if (oneByOne) {
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
    function removeAsked() {
        const sources = removalAsked
        removalAsked = []
        if (sources.length === 0 || changeLocked)
            return
        if (typeof provider.removeWatchRows !== "function") {
            sources.slice().sort((a, b) => b - a).forEach(source => provider.removeWatch(source))
            say(qsTr("Removed %Ln values", "", sources.length), 0)
            selectCurrent()
            return
        }
        const result = provider.removeWatchRows(sources)
        if (result && result.status === "ok" && result.changed > 0) {
            say(qsTr("Removed %Ln values", "", result.changed), typeof result.serial === "number" ? result.serial : 0)
            selectCurrent()
        }
    }
    // After a removal the row the cursor moved to is the one selected.
    function selectCurrent() {
        if (selectable && watchList.currentIndex >= 0)
            rows.select(watchList.currentIndex)
        announcedCount = selectedCount
    }
    // Takes back the history's last step and says what it did: "Undone:
    // Remove device.Config.Gain from the Watchlist", with what could not be
    // put back.
    function undo() {
        if (history === null || !history.canUndo)
            return false
        const text = history.undoText
        let said = ""
        const heard = (serial, step, undone, note, outcome) => { said = history.sentence(step, undone, note, outcome) }
        history.done.connect(heard)
        const done = history.undo()
        history.done.disconnect(heard)
        if (done)
            forgetMeasured()
        say(said.length > 0 ? said : done ? qsTr("Undone: %1").arg(text) : "", 0)
        return done
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
        recorded = 0
        if (numbers.length > 0 && numbers.every(value => plotTraces.isPlotted(value.path))) {
            plotTraces.unplotMany(numbers.map(value => value.path))
            say(numbers.length === 1 ? qsTr("%1 is off the Plot; its readings are kept").arg(numbers[0].path)
                                     : qsTr("%L1 values are off the Plot; their readings are kept").arg(numbers.length),
                recorded)
            return true
        }
        const plotted = plotTraces.plotMany(numbers.map(value => value.path))
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
    readonly property bool viewInBar: treeAvailable
                                      && availableWidth >= barBase + formatButtonWidth + viewSegments.implicitWidth + barGap
    readonly property real barCore: barBase + (viewInBar ? viewSegments.implicitWidth + barGap : 0) + formatButtonWidth
    readonly property bool readOnceInBar: (viewInBar || !treeAvailable)
                                          && availableWidth >= barCore + readOnce.barWidth + barGap
    readonly property bool formatInBar: formatAvailable && readOnceInBar
        && availableWidth >= barCore - formatButtonWidth + formatSegmentsWidth + readOnce.barWidth + barGap
    readonly property bool everyShown: readOnceInBar && (formatInBar || !formatAvailable)
        && availableWidth >= barCore - formatButtonWidth + formatSegmentsWidth + readOnce.barWidth
                             + every.implicitWidth + 2 * barGap
    // The row family's columns (family A1-A3): Name fills; Value, Type and
    // Status are as wide as the widest text they have shown, growing only
    // until the list is cleared or the zoom changes, so a reading never makes
    // them jump; rw is two letters. As the room for names shrinks, rw hides
    // below 140 px, Type narrows to 40 px below 120 and hides below 96, then
    // Status becomes a glyph whose word is in its tooltip. Name, Value and ×
    // never hide, and a row is one line except while it is edited.
    readonly property real columnGap: Math.round(Type.bodyPixels * 0.9)
    readonly property real removeColumnWidth: Type.rowHeight
    readonly property real listWidth: Math.max(0, watchList.width - scrollReserve)
    property real valueSeen: 0
    property real typeSeen: 0
    property real statusSeen: 0
    property bool accessSeen: false
    property real valuePending: 0
    property real typePending: 0
    property real statusPending: 0
    property bool accessPending: false
    function noteNumber(width) {
        if (width > numberSeen)
            numberSeen = width
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
    // Forgets the widths (a cleared list, a new zoom) and asks every row to
    // measure itself again: a type's text never changes, so its row would
    // not report it a second time by itself.
    property int measureEpoch: 0
    function forgetMeasured() {
        valuePending = typePending = statusPending = 0
        accessPending = false
        valueSeen = typeSeen = statusSeen = numberSeen = 0
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
            panel.statusSeen = Math.max(panel.statusSeen, panel.statusPending)
            panel.accessSeen = panel.accessSeen || panel.accessPending
        }
    }
    onCountChanged: if (count === 0) forgetMeasured()
    Connections {
        target: Type
        function onBodyPixelsChanged() { panel.forgetMeasured() }
    }
    // Numbers never elide: the column is as wide as the widest one, up to
    // more than half of a narrow list.
    // Room for the pencil at the value's left while rows can be written, so
    // it never sits on the digits.
    readonly property real pencilRoom: provider.canWriteWatch === true ? Type.rowHeight : 0
    property real numberSeen: 0
    readonly property real valueColumn: count === 0 ? 0
        : Math.max(Math.round(Type.bodyPixels * 4.2),
                   Math.min(Math.ceil(numberSeen) + pencilRoom, listWidth * 0.55),
                   Math.min(Math.ceil(valueSeen) + pencilRoom, Type.bodyPixels * 16.5, listWidth * 0.4))
    readonly property real typeFull: typeSeen > 0 ? Math.max(Type.bodyPixels * 3, Math.min(Math.ceil(typeSeen), Type.bodyPixels * 9.6)) : 0
    readonly property real accessFull: accessSeen ? Math.round(Type.bodyPixels * 1.05) : 0
    TextMetrics { id: statusTitleMetrics; font.family: panel.font.family; font.pointSize: Type.caption; text: qsTr("Status") }
    readonly property real statusFull: statusSeen > 0 ? Math.ceil(Math.max(statusSeen, statusTitleMetrics.advanceWidth)) : 0
    readonly property real glyphWidth: Math.round(Type.bodyPixels * 1.05)
    function nameRoom(type, access, status) {
        return listWidth - Type.padding - valueColumn - removeColumnWidth - 2 * columnGap
            - (treeView ? 3 * Type.indent : 0)
            - (type > 0 ? type + columnGap : 0) - (access > 0 ? access + columnGap : 0)
            - (status > 0 ? status + columnGap : 0)
    }
    readonly property real accessColumn: accessFull > 0 && nameRoom(typeFull, accessFull, statusFull) >= Type.bodyPixels * 10.5
                                         ? accessFull : 0
    readonly property real typeColumn: typeFull <= 0 ? 0
        : nameRoom(typeFull, 0, statusFull) >= Type.bodyPixels * 9 ? typeFull
        : nameRoom(Type.bodyPixels * 3, 0, statusFull) >= Type.bodyPixels * 7.2 ? Type.bodyPixels * 3 : 0
    readonly property bool statusGlyph: statusFull > 0 && nameRoom(typeColumn, accessColumn, statusFull) < Type.bodyPixels * 7.2
    readonly property real statusColumn: statusFull <= 0 ? 0 : statusGlyph ? glyphWidth : statusFull
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

    padding: Type.padding
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
                text: qsTr("Clear the Watchlist…")
                enabled: panel.count > 0 && !panel.changeLocked
                onTriggered: clearDialog.open()
            }
        }

        // The status under the toolbar, wrapped: a failure's advice is read
        // whole, however long, and the toolbar does not move as it changes.
        Label {
            objectName: "watchStatusLabel"
            visible: (panel.statusShown || panel.readManyAdvice || panel.paceText.length > 0) && text.length > 0
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
            HoverHandler { id: statusHover }
            PlainToolTip {
                objectName: "watchPaceTip"
                visible: statusHover.hovered && parent.tip.length > 0
                delay: 700
                text: parent.tip
            }
        }
        // The outcome of the last write from a row, in words; or why the row
        // asked to write can't be.
        // A write the device confirmed reads quietly; one it reads back
        // otherwise, or that could not be read back or was refused, stands
        // out (WL-5).
        RowLayout {
            visible: panel.open && writeOutcome.text.length > 0
            Layout.fillWidth: true
            spacing: Type.tight
            Label {
                id: writeOutcome
                objectName: "watchWriteOutcome"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.ceil(contentHeight)
                textFormat: Text.PlainText
                text: panel.writeRefusalText.length > 0 ? panel.writeRefusalText
                    : panel.provider.watchWriteOutcome === undefined ? "" : panel.provider.watchWriteOutcome
                readonly property string kind: panel.writeRefusalText.length > 0 ? "refused"
                    : typeof panel.provider.watchWriteKind === "string" ? panel.provider.watchWriteKind : ""
                color: kind === "differs" || kind === "unconfirmed" || kind === "refused" ? Theme.textPrimary
                                                                                           : Theme.textSecondary
                font.pointSize: Type.caption
                wrapMode: Text.Wrap
            }
            LinkButton {
                objectName: "watchAllowChanges"
                visible: panel.writeRefusalText.length > 0 && panel.writeRefusalChanges &&
                         panel.provider.allowMutations === false
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
        // What the last removal or copy did, as the Device tree says it:
        // "Removed 3 values of device.Config · Undo", Undo while nothing else
        // changed; "Copied device.Config.Gain".
        RowLayout {
            objectName: "watchNoticeLine"
            visible: panel.open && panel.noticeText.length > 0
            Layout.fillWidth: true
            spacing: Type.spacing
            Label {
                objectName: "watchNotice"
                // As wide as its words, cut only when the line is too narrow.
                Layout.minimumWidth: 0
                textFormat: Text.PlainText
                text: panel.noticeText
                color: Theme.textSecondary
                font.pointSize: Type.caption
                elide: Text.ElideRight
            }
            Label {
                id: removedDot
                visible: removedUndo.visible
                text: "·"
                color: Theme.textMuted
                font.pointSize: Type.caption
                Accessible.ignored: true
            }
            LinkButton {
                id: removedUndo
                objectName: "watchUndo"
                visible: panel.canUndo
                text: qsTr("Undo")
                Accessible.name: panel.history ? qsTr("Undo: %1").arg(panel.history.undoText) : text
                font.pointSize: Type.caption
                onClicked: panel.undo()
            }
            Item { Layout.fillWidth: true }
        }
        // Column titles over the one-line rows; they stay while the rows scroll.
        RowLayout {
            objectName: "watchColumns"
            visible: panel.open && panel.count > 0
            Layout.fillWidth: true
            Layout.rightMargin: panel.scrollReserve
            Layout.topMargin: Type.tight
            // The row family's gap: "230" and "OK" read as two things.
            spacing: panel.columnGap
            // The list by name, A-Z, and back to the order added (WL-2c); the
            // tree keeps the device's order, so its header only names.
            Label {
                id: nameHeader
                objectName: "watchColumnName"
                readonly property bool sortable: panel.sortable && !panel.treeView
                text: sortable && panel.sorted ? qsTr("Name") + " \u25B4" : qsTr("Name")
                color: nameHeaderHover.hovered && sortable ? Theme.textPrimary : Theme.textSecondary
                font.pointSize: Type.caption
                // A target as tall as a row; the text stays where it was.
                leftPadding: Type.tight
                topPadding: Type.tight
                bottomPadding: Type.tight
                Layout.leftMargin: Type.padding - Type.tight
                Layout.fillWidth: true
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
            Label { objectName: "watchColumnValue"; text: qsTr("Value"); color: Theme.textSecondary; font.pointSize: Type.caption; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: panel.valueColumn; elide: Text.ElideRight
                    Accessible.role: Accessible.ColumnHeader }
            Label { objectName: "watchColumnType"; visible: panel.typeColumn > 0; text: qsTr("Type"); color: Theme.textSecondary; font.pointSize: Type.caption; Layout.preferredWidth: panel.typeColumn; elide: Text.ElideRight
                    Accessible.role: Accessible.ColumnHeader }
            Label { objectName: "watchColumnAccess"; visible: panel.accessColumn > 0; text: qsTr("rw"); color: Theme.textSecondary; font.pointSize: Type.caption; Layout.preferredWidth: panel.accessColumn
                    Accessible.role: Accessible.ColumnHeader; Accessible.name: qsTr("Access")
                    ToolTip.visible: accessTitleHover.hovered; ToolTip.delay: 700; ToolTip.text: qsTr("Access: r can be read, w can be written")
                    HoverHandler { id: accessTitleHover } }
            Label { objectName: "watchColumnStatus"; visible: panel.statusColumn > 0; text: panel.statusGlyph ? "" : qsTr("Status"); color: Theme.textSecondary; font.pointSize: Type.caption; Layout.preferredWidth: panel.statusColumn }
            Item { Layout.preferredWidth: panel.removeColumnWidth }
        }
        Rectangle {
            objectName: "watchColumnsRule"
            visible: panel.open && panel.count > 0
            Layout.fillWidth: true
            Layout.rightMargin: panel.scrollReserve
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
                panel.writeRefusalText = ""
                panel.writeRefusalChanges = false
            }
            objectName: "watchlistView"
            Layout.fillWidth: true
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
                // Optional roles (writable, sampleValue, sampleExact) are read
                // through the model object, so other providers need not have them.
                required property var model
                width: watchList.width
                // A writable Field is changed in its own row, as in Live
                // Expressions: double-click the value (or the pencil, or F2),
                // Enter writes, Esc or clicking elsewhere cancels.
                readonly property bool writable: model.writable === true &&
                                                 panel.provider.canWriteWatch === true
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
                // the Field; its members leave Status and rw blank.
                readonly property bool nested: model.nested === true
                readonly property int sourceRow: typeof model.sourceRow === "number" ? model.sourceRow : index
                property bool editing: false
                property bool manualCode: false
                // A value inside a struct or array Field is written by writing
                // its whole Field: what that covers, and why it can't be.
                readonly property var writeScope: model.writeScope && typeof model.writeScope.field === "string"
                                                  && model.writeScope.field.length > 0 ? model.writeScope : null
                readonly property string writeRefusal: typeof model.writeRefusal === "string" ? model.writeRefusal : ""
                // The write reads the Field again first: the other values keep
                // what the device holds then, not what the row last showed.
                readonly property string scopeText: {
                    if (!writeScope)
                        return ""
                    const one = writeScope.others === 1
                    switch (writeScope.kind) {
                    case "elements":
                        return one ? qsTr("Writes all of %1; its other element keeps its value.").arg(writeScope.field)
                                   : qsTr("Writes all of %1; its other %L2 elements keep their values.")
                                         .arg(writeScope.field).arg(writeScope.others)
                    case "values":
                        return one ? qsTr("Writes all of %1; its other value keeps its value.").arg(writeScope.field)
                                   : qsTr("Writes all of %1; its other %L2 values keep their values.")
                                         .arg(writeScope.field).arg(writeScope.others)
                    default:
                        return one ? qsTr("Writes all of %1; its other member keeps its value.").arg(writeScope.field)
                                   : qsTr("Writes all of %1; its other %L2 members keep their values.")
                                         .arg(writeScope.field).arg(writeScope.others)
                    }
                }
                readonly property string overwriteText: writeScope
                    ? qsTr("If the device changes another value of %1 in between, that change is overwritten.")
                          .arg(writeScope.field)
                    : ""
                property string draftText: ""
                readonly property string editorKind: typeof model.editorKind === "string" ? model.editorKind : "text"
                readonly property bool choices: editorKind === "bool" || editorKind === "enum"
                readonly property bool choicesTruncated: model.editorChoicesTruncated === true
                readonly property var choiceOptions: {
                    const rows = []
                    const source = model.editorChoices
                    if (source && typeof source.length === "number") {
                        for (let i = 0; i < source.length; ++i) {
                            const item = source[i]
                            if (item && typeof item.label === "string" && typeof item.text === "string")
                                rows.push({label: item.label, text: item.text, manual: false})
                        }
                    }
                    if (editorKind === "enum") {
                        if (draftText.length > 0 && !rows.some(item => item.text === draftText))
                            rows.unshift({label: (choicesTruncated ? qsTr("Code (%1)") : qsTr("Unknown (%1)"))
                                .arg(draftText), text: draftText, manual: false})
                        rows.push({label: qsTr("Enter code…"), text: "", manual: true})
                    }
                    return rows
                }
                readonly property int choiceIndex: choiceOptions.findIndex(item => !item.manual && item.text === draftText)
                function startEdit() {
                    if (!writable && isValue) {
                        panel.sayNotWritable(watchRow)
                        return
                    }
                    if (!writable || editing)
                        return
                    watchList.currentIndex = index
                    if (panel.selectable)
                        panel.rows.select(index)
                    panel.writeRefusalText = ""
                    panel.writeRefusalChanges = false
                    manualCode = false
                    // New providers own the exact typed text, including enum codes.
                    // Keep the legacy fallback only when the role is absent.
                    draftText = typeof model.editorText === "string" ? model.editorText
                        : model.sampleExact === true
                            ? (model.sampleValue === 0 && 1 / model.sampleValue < 0 ? "-0" : String(model.sampleValue))
                            : watchRow.text
                    valueEditor.text = draftText
                    editing = true
                    if (choices) valueChoice.forceActiveFocus()
                    else { valueEditor.forceActiveFocus(); valueEditor.selectAll() }
                }
                function commitEdit() {
                    if (!editing || !writable || draftText.length === 0)
                        return
                    if (editPreview.error.length > 0) {
                        valueEditor.Accessible.announce(editPreview.error)
                        return
                    }
                    const value = draftText
                    finishEdit()
                    panel.provider.writeWatch(watchRow.sourceRow, value)
                }
                // The list is a focus scope: the editor lets go of its focus
                // first, or the list would hand it back to the hidden editor.
                function finishEdit(restoreFocus = true) {
                    editing = false
                    valueChoice.popup.close()
                    valueEditor.focus = false
                    valueChoice.focus = false
                    editorScope.focus = false
                    if (restoreFocus) watchList.forceActiveFocus()
                }
                // One line of columns; a row being edited takes a second line
                // for its editor, under its name and ×.
                readonly property bool compact: editing
                // A reading not yet made, and one a newer reading should have
                // replaced (family A6).
                readonly property bool quiet: status === "" || status === "Pending" || status === "Not read" || status === "NotRead"
                readonly property bool waiting: isValue && quiet
                readonly property bool stale: status === "Stale"
                // Numbers, hex and bools line up on the right; enum names and a
                // folded node's summary on the left.
                // The form the row shows: its own, else the Watchlist's. A
                // node's own is the one its values share, "mixed" when not.
                readonly property string ownFormat: typeof model.format === "string" && model.format !== "mixed"
                                                    ? model.format : ""
                readonly property string formatName: ownFormat.length > 0 ? ownFormat : panel.format
                readonly property string shownText: typeof model.shownText === "string" && model.shownText.length > 0
                                                    ? model.shownText : text
                readonly property bool leftValue: !isValue || (editorKind === "enum" && formatName === "original")
                // A word only for a problem or a stale value; waiting is "–" alone,
                // its word in the value's tooltip and the row's description.
                // A value inside a Field shares its Field's word, but one its
                // device lacks says so itself.
                readonly property string statusText: ok || quiet || (nested && !missing) || isGroup ? "" : panel.statusWord(status)
                readonly property string waitingText: waiting ? panel.statusWord(status === "" ? "Not read" : status) : ""
                readonly property string accessText: !nested && typeof model.access === "string" ? model.access : ""
                readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                // Reports this row's widths again when the panel forgot them.
                readonly property int measureEpoch: panel.measureEpoch
                // After the change has settled (a new zoom's fonts); the timer
                // is the row's own, so a row destroyed meanwhile is not called.
                onMeasureEpochChanged: reportAgain.restart()
                Timer { id: reportAgain; interval: 0; onTriggered: watchRow.report() }
                // Numbers never elide; text and summaries do, within the cap.
                function reportValue() {
                    // A node's summary uses the name's room, never the column's.
                    if (!watchRow.isValue)
                        return
                    if (watchRow.leftValue)
                        panel.noteMeasured(valueText.implicitWidth - valueText.leftPadding, 0, 0, false)
                    else
                        panel.noteNumber(valueText.implicitWidth - valueText.leftPadding)
                }
                function report() {
                    watchRow.reportValue()
                    panel.noteMeasured(0,
                                       typeMetrics.text.length > 0 ? typeMetrics.advanceWidth + Type.tight : 0,
                                       statusMetrics.text.length > 0 ? statusMetrics.advanceWidth + Type.tight : 0,
                                       accessText.length > 0)
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
                readonly property bool active: marked && (watchList.activeFocus || rowMenu.visible || removeDialog.visible)
                // Muted text steps up on the quiet fill, to stay readable.
                readonly property color muted: marked && !active ? Theme.textSecondary : Theme.textMuted
                readonly property bool removeShown: compact || rowHover.hovered || current
                readonly property color foreground: active ? Theme.selectedForeground : Theme.textPrimary
                readonly property real valueHeight: editing ? Math.max(Type.rowHeight, editorScope.implicitHeight) : Type.rowHeight
                // What a keyboard or screen reader user hears: the hidden
                // columns too, and the status the glyph only marks (spec F).
                Accessible.role: panel.treeView ? Accessible.TreeItem : Accessible.ListItem
                Accessible.selectable: panel.selectable
                Accessible.selected: watchRow.marked
                Accessible.expandable: watchRow.branch
                Accessible.expanded: watchRow.branch && watchRow.expanded
                // What a nested row belongs to: "Config", "Phase[2]" (WL-2c).
                readonly property string parentLabel: typeof model.parentLabel === "string" ? model.parentLabel : ""
                Accessible.name: [panel.treeView ? watchRow.name.substring(nameCell.leafStart) : watchRow.name,
                                  watchRow.parentLabel.length === 0 ? ""
                                  : watchRow.name.charAt(nameCell.leafStart) === "["
                                    ? qsTr("element of %1").arg(watchRow.parentLabel)
                                    : qsTr("member of %1").arg(watchRow.parentLabel),
                                  watchRow.isGroup ? qsTr("group, %L1 watched").arg(watchRow.leaves)
                                  : watchRow.isValue ? ""
                                  : watchRow.node === "array" ? qsTr("array, %L1 watched").arg(watchRow.leaves)
                                  : watchRow.node === "element" ? qsTr("struct, %L1 watched").arg(watchRow.leaves)
                                  : qsTr("struct, %L1 watched").arg(watchRow.leaves),
                                  watchRow.shownText.length > 0 ? watchRow.shownText : watchRow.isValue ? "–" : "",
                                  watchRow.typeText,
                                  watchRow.accessText === "rw" ? qsTr("read and write")
                                  : watchRow.accessText === "r-" ? qsTr("read only") : ""]
                    .filter(part => part.length > 0).join(", ")
                Accessible.description: [watchRow.statusText.length > 0 ? watchRow.statusText : watchRow.waitingText,
                                         watchRow.statusDetail,
                                         watchRow.isValue && watchRow.ownFormat.length > 0
                                             ? qsTr("shown in %1").arg(panel.formatWord(watchRow.ownFormat)) : ""]
                    .filter(part => part.length > 0).join(", ")
                height: compact ? Type.rowHeight + valueHeight : valueHeight
                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: panel.scrollReserve
                    color: watchRow.active ? Theme.stateSelected
                         : watchRow.marked ? Theme.stateSelectedInactive
                         : rowHover.hovered ? Theme.stateHover
                         : watchRow.index % 2 ? Theme.surfaceAlternate : "transparent"
                }
                // The keyboard's row while the list has the keys: a frame inside
                // the row, the list clipping anything outside it.
                Rectangle {
                    objectName: "watchRowFocusFrame"
                    visible: watchRow.current && !watchRow.editing
                    anchors.fill: parent
                    anchors.margins: 1
                    anchors.rightMargin: panel.scrollReserve + 1
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
                // to the numbers' edge, so no column moves when a row folds.
                Text {
                    id: summaryText
                    objectName: "watchRowSummary"
                    visible: !watchRow.isValue && !watchRow.isGroup && watchRow.text.length > 0 && width > 0
                    readonly property real start: rowLayout.x + nameCell.x + leafText.x + leafText.width + panel.columnGap
                    x: start
                    y: 0
                    width: Math.max(0, rowLayout.x + valueText.x + valueText.width - start)
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
                    PlainToolTip { visible: summaryHover.hovered && summaryText.truncated; text: watchRow.text; delay: 700 }
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
                // One grid for both shapes: a table row is name, value, status,
                // ×; two lines put × after the name and the status after the
                // value. Every line is a row high and nothing wraps.
                GridLayout {
                    id: rowLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: panel.scrollReserve
                    anchors.verticalCenter: parent.verticalCenter
                    columns: watchRow.compact ? 2 : 6
                    rowSpacing: 0
                    columnSpacing: watchRow.compact ? Type.spacing : panel.columnGap
                    // The path: its prefix muted, its last segment whole while
                    // it fits, the prefix eliding in its middle (dev…Config.Gain).
                    Item {
                        id: nameCell
                        objectName: "watchRowName"
                        Layout.row: 0
                        Layout.column: 0
                        Layout.leftMargin: Type.padding + (panel.treeView ? (watchRow.depth + 1) * Type.indent : 0)
                        Layout.fillWidth: true
                        Layout.minimumWidth: 1
                        Layout.preferredHeight: Type.rowHeight
                        readonly property int leafStart: typeof watchRow.model.leafStart === "number"
                                                         ? Math.max(0, Math.min(watchRow.name.length, watchRow.model.leafStart)) : 0
                        Text {
                            id: prefixText
                            objectName: "watchRowPrefix"
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(0, Math.min(implicitWidth, parent.width - leafText.implicitWidth))
                            visible: text.length > 0 && width > 0
                            textFormat: Text.PlainText
                            text: panel.treeView ? "" : watchRow.name.substring(0, nameCell.leafStart)
                            font: panel.font
                            color: watchRow.active ? Theme.selectedForeground : watchRow.muted
                            elide: Text.ElideMiddle
                        }
                        Text {
                            id: leafText
                            objectName: "watchRowLeaf"
                            anchors.left: prefixText.visible ? prefixText.right : parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, parent.width - (prefixText.visible ? prefixText.width : 0))
                            textFormat: Text.PlainText
                            text: watchRow.name.substring(nameCell.leafStart)
                            font.family: panel.font.family
                            font.pointSize: panel.font.pointSize
                            font.weight: watchRow.isGroup ? Type.headingWeight : Font.Normal
                            color: watchRow.foreground
                            elide: Text.ElideRight
                        }
                        // Edited while polling (WL-5): the live reading beside the
                        // name, "Now 1.48"; a failing read says its status.
                        Text {
                            objectName: "watchRowNow"
                            anchors.left: leafText.right
                            anchors.leftMargin: Type.padding
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(0, parent.width - leafText.x - leafText.width - Type.padding)
                            visible: watchRow.editing && watchRow.isValue && panel.provider.watchActive === true && width > 0
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
                            visible: nameHover.hovered && (!watchRow.isValue || prefixText.truncated || leafText.truncated)
                            text: watchRow.isValue ? watchRow.name
                                : [watchRow.name, watchRow.typeText,
                                   watchRow.leaves === 1 ? qsTr("1 value watched") : qsTr("%Ln values watched", "", watchRow.leaves)]
                                  .filter(part => part.length > 0).join(" \u00B7 ")
                            delay: 700
                        }
                    }
                    // A group shows how many values it holds, at the row's right.
                    Label {
                        objectName: "watchGroupCount"
                        visible: watchRow.isGroup
                        Layout.row: 0
                        Layout.column: 1
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: qsTr("%L1 watched").arg(watchRow.leaves)
                        font.pointSize: Type.caption
                        color: watchRow.active ? Theme.selectedForeground : watchRow.muted
                    }
                    Label {
                        id: valueText
                        objectName: "watchValueText"
                        visible: !watchRow.editing && !watchRow.isGroup
                        Layout.row: 0
                        Layout.column: 1
                        Layout.preferredWidth: panel.valueColumn
                        Layout.minimumWidth: 1
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        // Not read yet: a dash at the value's alignment, no word.
                        text: !watchRow.isValue ? "" : watchRow.missing ? "–"
                            : watchRow.shownText.length > 0 ? watchRow.shownText : watchRow.waiting ? "–" : ""
                        font.family: Type.fixedFamily
                        font.italic: (watchRow.stale || watchRow.problem) && watchRow.text.length > 0
                        color: watchRow.active ? Theme.selectedForeground
                             : watchRow.ok ? Theme.textPrimary : watchRow.muted
                        horizontalAlignment: watchRow.leftValue ? Text.AlignLeft : Text.AlignRight
                        leftPadding: watchRow.leftValue ? panel.pencilRoom : 0
                        elide: Text.ElideRight
                        onImplicitWidthChanged: watchRow.reportValue()
                        Component.onCompleted: watchRow.reportValue()
                        ToolTip.visible: valueHover.hovered && (truncated || watchRow.waitingText.length > 0
                                                                || watchRow.ownFormat.length > 0)
                        ToolTip.text: watchRow.waitingText.length > 0 ? watchRow.waitingText
                                    : watchRow.ownFormat.length === 0 ? watchRow.shownText
                                    : watchRow.ownFormat === panel.format
                                      ? qsTr("%1 · shown in %2").arg(watchRow.shownText).arg(panel.formatWord(watchRow.ownFormat))
                                    : qsTr("%1 · shown in %2 (the Watchlist shows %3)").arg(watchRow.shownText)
                                          .arg(panel.formatWord(watchRow.ownFormat)).arg(panel.formatWord(panel.format))
                        HoverHandler { id: valueHover }
                        TapHandler {
                            id: valueTap
                            enabled: watchRow.writable || watchRow.writeRefusal.length > 0
                            onDoubleTapped: if (valueTap.point.modifiers === Qt.NoModifier) watchRow.startEdit()
                        }
                        ToolButton {
                            objectName: "watchEditButton"
                            text: "✎"
                            visible: watchRow.writable && (rowHover.hovered || visualFocus)
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Type.rowHeight
                            height: Type.rowHeight
                            padding: 0
                            focusPolicy: Qt.NoFocus
                            onClicked: watchRow.startEdit()
                            Accessible.name: qsTr("Write %1").arg(watchRow.name)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Write a new value (F2, or double-click the value)")
                        }
                    }
                    FocusScope {
                        id: editorScope
                        visible: watchRow.editing
                        Layout.row: watchRow.compact ? 1 : 0
                        Layout.column: watchRow.compact ? 0 : 1
                        Layout.columnSpan: watchRow.compact ? 2 : 1
                        Layout.fillWidth: true
                        Layout.minimumWidth: Type.bodyPixels * 4.5
                        Layout.preferredHeight: implicitHeight
                        implicitHeight: editorColumn.implicitHeight
                        onActiveFocusChanged: if (!activeFocus && watchRow.editing) cancelEdit.restart()
                        Timer {
                            id: cancelEdit
                            interval: 0
                            onTriggered: if (watchRow.editing && !editorScope.activeFocus && !valueChoice.popup.visible)
                                watchRow.finishEdit(false)
                        }
                        Timer {
                            id: manualFocus
                            interval: 0
                            onTriggered: if (watchRow.editing && watchRow.manualCode) {
                                valueEditor.forceActiveFocus()
                                valueEditor.selectAll()
                            }
                        }
                        ColumnLayout {
                            id: editorColumn
                            anchors.fill: parent
                            spacing: 0
                            RowLayout {
                                id: editorLine
                                Layout.fillWidth: true
                                spacing: Type.tight
                                ComboBox {
                                    id: valueChoice
                                    objectName: "watchValueChoice"
                                    visible: watchRow.choices && !watchRow.manualCode
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    model: watchRow.choiceOptions
                                    textRole: "label"
                                    valueRole: "text"
                                    currentIndex: watchRow.choiceIndex
                                    displayText: currentIndex < 0 ? qsTr("Choose a value") : currentText
                                    palette.button: Theme.buttonFill
                                    palette.buttonText: Theme.textPrimary
                                    palette.text: Theme.textPrimary
                                    palette.base: Theme.surface
                                    palette.light: Theme.stateSelected
                                    palette.midlight: Theme.stateHover
                                    palette.mid: Theme.buttonFillPressed
                                    palette.dark: Theme.textSecondary
                                    palette.highlight: Theme.stateSelected
                                    palette.highlightedText: Theme.selectedForeground
                                    delegate: ItemDelegate {
                                        id: choiceOption
                                        required property var model
                                        required property int index
                                        width: ListView.view ? ListView.view.width : implicitWidth
                                        text: model.label
                                        highlighted: valueChoice.highlightedIndex === index
                                        font.weight: valueChoice.currentIndex === index ? Type.headingWeight : Type.bodyWeight
                                        palette.text: Theme.textPrimary
                                        palette.highlightedText: Theme.selectedForeground
                                        palette.light: Theme.stateSelected
                                        palette.midlight: Theme.stateHover
                                        palette.highlight: Theme.stateSelected
                                        PlainToolTip { visible: choiceOption.hovered; text: choiceOption.text; delay: 700 }
                                    }
                                    Accessible.name: qsTr("New value for %1").arg(watchRow.name)
                                    Accessible.description: currentIndex < 0
                                        ? qsTr("Choose a value, then Write or press Enter. Escape cancels.")
                                        : qsTr("%1. Choose a value, then Write or press Enter. Escape cancels.").arg(currentText)
                                    PlainToolTip {
                                        objectName: "watchValueChoiceToolTip"
                                        visible: (valueChoice.hovered || valueChoice.visualFocus) && !valueChoice.popup.visible
                                        delay: 700
                                        text: valueChoice.Accessible.description
                                    }
                                    onActivated: function(index) {
                                        const item = watchRow.choiceOptions[index]
                                        if (!item) return
                                        if (item.manual) {
                                            valueEditor.text = watchRow.draftText
                                            watchRow.manualCode = true
                                            manualFocus.restart()
                                        } else watchRow.draftText = item.text
                                        currentIndex = Qt.binding(() => watchRow.choiceIndex)
                                    }
                                    Keys.onReturnPressed: event => {
                                        if (popup.visible) event.accepted = false
                                        else watchRow.commitEdit()
                                    }
                                    Keys.onEnterPressed: event => {
                                        if (popup.visible) event.accepted = false
                                        else watchRow.commitEdit()
                                    }
                                    Keys.onEscapePressed: watchRow.finishEdit()
                                    Connections {
                                        target: valueChoice.popup
                                        function onClosed() { cancelEdit.restart() }
                                    }
                                }
                                TextField {
                                    id: valueEditor
                                    objectName: "watchValueEditor"
                                    visible: !watchRow.choices || watchRow.manualCode
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.preferredHeight: Math.max(Type.rowHeight, implicitHeight)
                                    font.family: Type.fixedFamily
                                    horizontalAlignment: watchRow.compact ? TextInput.AlignLeft : TextInput.AlignRight
                                    placeholderText: watchRow.manualCode ? qsTr("Numeric code") : ""
                                    topPadding: Type.tight
                                    bottomPadding: Type.tight
                                    onTextEdited: watchRow.draftText = text
                                    Keys.onReturnPressed: watchRow.commitEdit()
                                    Keys.onEnterPressed: watchRow.commitEdit()
                                    Keys.onEscapePressed: watchRow.finishEdit()
                                    Accessible.name: qsTr("New value for %1").arg(watchRow.name)
                                    Accessible.description: (watchRow.writeScope
                                        ? qsTr("%1. %2 %3 Write all or press Enter to send it once. Escape cancels.")
                                            .arg(watchRow.draftText).arg(watchRow.scopeText).arg(watchRow.overwriteText)
                                        : qsTr("%1. Write or press Enter to send this value once. Escape cancels.").arg(watchRow.draftText))
                                        + (editPreview.text.length > 0 ? " " + editPreview.text : "")
                                    PlainToolTip {
                                        objectName: "watchValueEditorToolTip"
                                        visible: valueEditor.hovered || valueEditor.activeFocus
                                        delay: 700
                                        text: valueEditor.Accessible.description
                                    }
                                }
                                // What Enter would write in the other form, "= 0x40000000",
                                // or why not; the whole reason in the tooltip.
                                Label {
                                    id: editPreview
                                    objectName: "watchValuePreview"
                                    // Read again when the row's reading changes (its type may
                                    // only now be known) and on every key.
                                    readonly property var result: {
                                        void watchRow.text
                                        return watchRow.editing && valueEditor.visible
                                            && typeof panel.provider.watchEditPreview === "function"
                                            ? panel.provider.watchEditPreview(watchRow.sourceRow, watchRow.draftText) : null
                                    }
                                    readonly property string error: result && result.error ? result.error : ""
                                    visible: text.length > 0
                                    Layout.maximumWidth: Type.bodyPixels * 14
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    text: error.length === 0 && result && result.preview ? result.preview : ""
                                    font.family: Type.fixedFamily
                                    font.pointSize: Type.caption
                                    // The edited row is the current one: its selected colour.
                                    color: watchRow.active ? Theme.selectedForeground : watchRow.muted
                                    HoverHandler { id: previewHover }
                                    PlainToolTip { visible: previewHover.hovered && editPreview.truncated; text: editPreview.text; delay: 300 }
                                }
                                ToolButton {
                                    objectName: "watchValueChoiceReturn"
                                    visible: watchRow.manualCode
                                    text: "⌄"
                                    Accessible.name: qsTr("Choose a named value for %1").arg(watchRow.name)
                                    onClicked: {
                                        watchRow.manualCode = false
                                        valueChoice.forceActiveFocus()
                                        valueChoice.popup.open()
                                    }
                                }
                                Button {
                                    objectName: "watchValueWrite"
                                    Layout.preferredWidth: implicitContentWidth + leftPadding + rightPadding
                                    text: watchRow.writeScope ? qsTr("Write all") : qsTr("Write")
                                    Layout.minimumWidth: implicitWidth
                                    enabled: watchRow.writable && watchRow.draftText.length > 0 && editPreview.error.length === 0
                                    Accessible.name: qsTr("Write %1 once").arg(watchRow.name)
                                    Accessible.description: watchRow.scopeText
                                    onClicked: watchRow.commitEdit()
                                }
                            }
                            // Line 2 of a value's editor: why the typed value can't be
                            // written - a mark in the warning colour, the words in the
                            // row's own - before what Write all covers.
                            RowLayout {
                                objectName: "watchValueError"
                                visible: editPreview.error.length > 0
                                Layout.fillWidth: true
                                Layout.leftMargin: Type.padding
                                spacing: Type.tight
                                Text {
                                    text: "⚠"
                                    color: Theme.warning
                                    font.pointSize: Type.caption
                                    Layout.alignment: Qt.AlignTop
                                    Accessible.ignored: true
                                }
                                Label {
                                    objectName: "watchValueErrorText"
                                    Layout.fillWidth: true
                                    textFormat: Text.PlainText
                                    text: editPreview.error
                                    color: watchRow.active ? Theme.selectedForeground : Theme.textPrimary
                                    font.pointSize: Type.caption
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }
                            // Line 2 of a value's editor: what Write all covers.
                            Label {
                                objectName: "watchWriteScope"
                                visible: watchRow.writeScope !== null
                                Layout.fillWidth: true
                                Layout.leftMargin: Type.padding
                                Layout.preferredHeight: visible ? Math.max(Type.rowHeight, Math.ceil(contentHeight) + 2 * Type.tight) : 0
                                verticalAlignment: Text.AlignVCenter
                                textFormat: Text.PlainText
                                text: watchRow.scopeText
                                // The edited row is the current one: all of it in its selected colour.
                                color: watchRow.active ? Theme.selectedForeground : Theme.textSecondary
                                font.pointSize: Type.caption
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                HoverHandler { id: scopeHover }
                                PlainToolTip {
                                    visible: scopeHover.hovered && watchRow.writeScope !== null
                                    delay: 700
                                    text: watchRow.writeScope
                                        ? qsTr("%1 is written whole: Write all reads it, sets %2 in it, writes all of it and reads it back. %3")
                                            .arg(watchRow.writeScope.field).arg(watchRow.name).arg(watchRow.overwriteText)
                                        : ""
                                }
                            }
                        }
                    }
                    Label {
                        objectName: "watchRowType"
                        visible: !watchRow.editing && !watchRow.isGroup && panel.typeColumn > 0
                        Layout.row: 0
                        Layout.column: 2
                        Layout.preferredWidth: panel.typeColumn
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: watchRow.typeText
                        font.family: Type.fixedFamily
                        font.pointSize: Type.caption
                        color: watchRow.active ? Theme.selectedForeground : Theme.textSecondary
                        elide: Text.ElideRight
                        HoverHandler { id: typeHover }
                        PlainToolTip { visible: typeHover.hovered && parent.truncated; text: watchRow.typeText; delay: 700 }
                    }
                    // rw: each letter, a missing right as "-" in the placeholder colour.
                    Row {
                        objectName: "watchRowAccess"
                        visible: !watchRow.editing && !watchRow.isGroup && panel.accessColumn > 0
                        Layout.row: 0
                        Layout.column: 3
                        Layout.preferredWidth: panel.accessColumn
                        Layout.preferredHeight: Type.rowHeight
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
                            text: watchRow.accessText === "rw" ? qsTr("read and write") : qsTr("read only")
                            delay: 700
                        }
                    }
                    // A reading's word only when it is not a fresh value: a
                    // problem in warning, waiting in muted; the provider's
                    // own words in the tooltip.
                    Label {
                        objectName: "watchRowStatus"
                        visible: !watchRow.editing && !watchRow.isGroup && panel.statusColumn > 0 && !panel.statusGlyph
                        Layout.row: 0
                        Layout.column: 4
                        Layout.minimumWidth: 1
                        Layout.preferredWidth: panel.statusColumn
                        Layout.preferredHeight: Type.rowHeight
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
                            visible: statusRowHover.hovered && parent.text.length > 0
                                     && (parent.truncated || parent.text !== watchRow.status || watchRow.statusDetail.length > 0)
                            text: watchRow.statusDetail.length > 0 ? watchRow.statusDetail : watchRow.status
                            delay: 700
                        }
                    }
                    // Too little room: the word becomes a mark, the word in its tooltip.
                    Item {
                        objectName: "watchRowStatusGlyph"
                        visible: !watchRow.editing && !watchRow.isGroup && panel.statusGlyph
                        Layout.row: 0
                        Layout.column: 4
                        Layout.preferredWidth: panel.statusColumn
                        Layout.preferredHeight: Type.rowHeight
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
                        id: statusMetrics
                        font.family: panel.font.family
                        font.pointSize: Type.caption
                        text: watchRow.statusText
                        onAdvanceWidthChanged: panel.noteMeasured(0, 0, text.length > 0 ? advanceWidth + Type.tight : 0, false)
                    }
                    // × shows on the row under the pointer and on the current
                    // row; the keyboard removes with Delete.
                    Item {
                        Layout.row: 0
                        Layout.column: watchRow.compact ? 1 : 5
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredWidth: panel.removeColumnWidth
                        Layout.preferredHeight: Type.rowHeight
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
                            onClicked: panel.removeAt(watchRow.index, false, true)
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
            text: (rowMenu.many === 0 ? qsTr("Remove")
                   : panel.history === null || typeof panel.provider.removeWatchRows !== "function"
                     ? qsTr("Remove %Ln values…", "", rowMenu.many) : qsTr("Remove %Ln values", "", rowMenu.many))
                  + "\t" + qsTr("Delete")
            enabled: !panel.changeLocked
            onTriggered: panel.removeAt(panel.menuRow)
        }
    }

    // Clearing asks first. Cancel has the focus, so Enter keeps the list;
    // Remove all is drawn in the danger colour, not the accent.
    Dialog {
        id: clearDialog
        objectName: "clearWatchDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        // A width of its own: sized from its title and buttons, a dialog and
        // its button row would each wait for the other.
        width: Math.min(Type.bodyPixels * 30, parent ? parent.width - 2 * Type.groupSpacing : Type.bodyPixels * 30)
        modal: true
        palette.window: Theme.surface
        palette.windowText: Theme.textPrimary
        palette.base: Theme.surface
        palette.text: Theme.textPrimary
        palette.button: Theme.buttonFill
        palette.buttonText: Theme.textPrimary
        palette.highlight: Theme.stateSelected
        palette.highlightedText: Theme.selectedForeground
        title: panel.count === 1 ? qsTr("Remove the one value from the Watchlist?")
                                 : qsTr("Remove all %L1 values from the Watchlist?").arg(panel.count)
        // No body: the question is the title, the answer the buttons.
        topPadding: 0
        bottomPadding: 0
        footer: DialogButtonBox {
            Button {
                objectName: "clearWatchConfirm"
                text: panel.count === 1 ? qsTr("Remove") : qsTr("Remove all")
                palette.buttonText: Theme.error
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }
            Button {
                id: clearCancel
                objectName: "clearWatchCancel"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            onDiscarded: {
                clearDialog.close()
                panel.provider.clearWatch()
            }
        }
        onOpened: clearCancel.forceActiveFocus(Qt.TabFocusReason)
    }
    // Many values removed with no history to put them back: no Undo, so the
    // panel asks first, as Clear does. UX-B removes both questions.
    Dialog {
        id: removeDialog
        objectName: "removeWatchDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(Type.bodyPixels * 30, parent ? parent.width - 2 * Type.groupSpacing : Type.bodyPixels * 30)
        modal: true
        palette.window: Theme.surface
        palette.windowText: Theme.textPrimary
        palette.base: Theme.surface
        palette.text: Theme.textPrimary
        palette.button: Theme.buttonFill
        palette.buttonText: Theme.textPrimary
        palette.highlight: Theme.stateSelected
        palette.highlightedText: Theme.selectedForeground
        title: qsTr("Remove %Ln values from the Watchlist?", "", panel.removalAsked.length)
        // In a layout, as in HexPane and UnsavedChangesDialog: a wrapped label
        // straight in a dialog loops its implicit height through the popup's
        // content size once the window is narrower than the dialog.
        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Undo cannot bring them back.")
                color: Theme.textSecondary
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "removeWatchConfirm"
                text: qsTr("Remove")
                palette.buttonText: Theme.error
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }
            Button {
                id: removeCancel
                objectName: "removeWatchCancel"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            onDiscarded: {
                panel.removeAsked()
                removeDialog.close()
            }
        }
        onOpened: removeCancel.forceActiveFocus(Qt.TabFocusReason)
        // Cancel or Escape: the rows asked about are forgotten. (Not on close:
        // a Remove closes the dialog before its own signal comes.)
        onRejected: panel.removalAsked = []
    }
}
