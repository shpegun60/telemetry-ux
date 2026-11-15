import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

ApplicationWindow {
    width: 480
    height: 420
    visible: true
    color: Theme.surfaceWindow
    // A stand-in for a host's UndoHistory (UR-1): steps with their words and
    // serials, the last taken back first - what the panel's notice and its
    // Ctrl+Z read and call.
    QtObject {
        id: fakeHistory
        objectName: "fakeHistory"
        property var steps: []
        property int serial: 0
        readonly property bool canUndo: steps.length > 0
        readonly property string undoText: steps.length > 0 ? steps[steps.length - 1].text : ""
        readonly property int undoSerial: steps.length > 0 ? steps[steps.length - 1].serial : 0
        signal recorded(int serial)
        signal done(int serial, string text, bool undone, string note, string outcome)
        function record(text, undo) {
            serial += 1
            steps = steps.concat([{ text: text, serial: serial, undo: undo }])
            recorded(serial)
            return serial
        }
        function undo() {
            if (steps.length === 0)
                return false
            const step = steps[steps.length - 1]
            steps = steps.slice(0, steps.length - 1)
            step.undo()
            done(step.serial, step.text, true, "", "done")
            return true
        }
        function sentence(text, undone, note, outcome) { return (undone ? "Undone: " : "Redone: ") + text }
    }
    QtObject {
        id: fixtureProvider
        readonly property var watchlist: watchTreeModel
        readonly property var watchRows: watchTreeRows
        readonly property int watchCount: 5
        property bool connected: true
        property bool watchActive: false
        readonly property bool watchPending: false
        property bool canWriteWatch: true
        property bool allowMutations: true
        property bool watchWriting: false
        readonly property bool canAddWatch: false
        readonly property bool watchReadManySupported: true
        readonly property bool watchRequiresExplicitIndividual: false
        property bool watchIndividual: false
        property int watchIntervalMs: 1000
        readonly property int watchIntervalMinimumMs: 1
        property string watchStatus: ""
        property var watchPace: ({})
        property string watchWriteOutcome: ""
        property string watchWriteKind: ""
        function writeWatch(row, value) { watchTreeCalls.write(row, value) }
        // Its changes go to the history as steps; null, nothing is recorded.
        property var history: fakeHistory
        function record(text, undo) { return history ? history.record(text, undo) : 0 }
        property string watchFormat: "original"
        // As the provider's: moves with every change of the rows or a format.
        property int watchRevision: 0
        function formatChanged() { ++watchRevision }
        onWatchFormatChanged: formatChanged()
        function chooseWatchFormat(name) {
            const before = watchFormat
            watchFormat = name
            record("Show the Watchlist in " + name, () => { watchFormat = before })
            return true
        }
        function checkWatchPath(text) { return watchTreeCalls.check(text) }
        function watchEditPreview(row, text) {
            return text === "2" ? { preview: "= 0x40000000", error: "" }
                 : text === "x" ? { preview: "", error: "x is not a number a f32 holds." }
                 : { preview: "", error: "" }
        }
        function watchPathCandidates(text) { return watchTreeCalls.candidates(text) }
        function addWatchPaths(lines) {
            ++watchRevision
            const result = watchTreeCalls.addPaths(lines)
            result.serial = record("Add " + lines.length + " values to the Watchlist", () => { watchTreeCalls.undo(); ++watchRevision })
            return result
        }
        // The rows' own formats by a stand-in path, as the provider keeps them by path.
        property var rowFormats: ({})
        function setWatchRowFormat(rows, format) {
            watchTreeCalls.format(rows, format)
            const before = Object.assign({}, rowFormats)
            record("Show " + rows.length + " values in " + format, () => { rowFormats = before; formatChanged() })
            const next = Object.assign({}, rowFormats)
            for (const row of rows) {
                if (format === "")
                    delete next["row" + row]
                else
                    next["row" + row] = format
            }
            rowFormats = next
            formatChanged()
            return true
        }
        function watchRowFormats() { return Object.assign({}, rowFormats) }
        function setWatchRowFormats(formats) {
            rowFormats = Object.assign({}, formats)
            formatChanged()
            return true
        }
        readonly property int watchOwnFormatCount: Object.values(rowFormats).filter(format => format !== watchFormat).length
        function watchFormatsDiffer() {
            return watchFormat !== "original" || Object.values(rowFormats).some(format => format !== "original")
        }
        function resetWatchFormats() {
            const rows = watchOwnFormatCount
            if (watchFormat === "original" && Object.keys(rowFormats).length === 0)
                return { status: "unchanged", rows: 0 }
            const before = { format: watchFormat, rows: Object.assign({}, rowFormats) }
            rowFormats = ({})
            watchFormat = "original"
            ++watchRevision
            const serial = record("Reset number formats to Original", () => {
                rowFormats = before.rows
                watchFormat = before.format
                ++watchRevision
            })
            return { status: "ok", rows: rows, serial: serial }
        }
        function removeWatch(row) { removeWatchRows([row]) }
        function removeWatchRows(rows) {
            watchTreeCalls.remove(rows)
            ++watchRevision
            // As the provider's: a step even with no device, the rows put back by path.
            const serial = record("Remove " + rows.length + " values from the Watchlist", () => {
                watchTreeCalls.undo()
                ++watchRevision
            })
            return { status: "ok", changed: rows.length, serial: serial }
        }
        function refreshWatch() {}
        function startWatch() {}
        function stopWatch() {}
        function clearWatch() {}
        function addWatch() {}
    }
    WatchlistPanel {
        objectName: "watchTreePanel"
        anchors.fill: parent
        provider: fixtureProvider
        collapsible: false
        framed: false
    }
}
