import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

ApplicationWindow {
    width: 480
    height: 420
    visible: true
    color: Theme.surfaceWindow
    QtObject {
        id: fixtureProvider
        readonly property var watchlist: watchTreeModel
        readonly property var watchRows: watchTreeRows
        readonly property int watchCount: 5
        readonly property bool connected: true
        readonly property bool watchActive: false
        readonly property bool watchPending: false
        readonly property bool canWriteWatch: true
        readonly property bool canAddWatch: false
        readonly property bool watchReadManySupported: true
        readonly property bool watchRequiresExplicitIndividual: false
        property bool watchIndividual: false
        property int watchIntervalMs: 1000
        readonly property int watchIntervalMinimumMs: 1
        readonly property string watchStatus: ""
        readonly property string watchWriteOutcome: ""
        function writeWatch(row, value) { watchTreeCalls.write(row, value) }
        property bool canUndoWatch: false
        property string watchFormat: "original"
        function checkWatchPath(text) { return watchTreeCalls.check(text) }
        function watchEditPreview(row, text) {
            return text === "2" ? { preview: "= 0x40000000", error: "" }
                 : text === "x" ? { preview: "", error: "x is not a number a f32 holds." }
                 : { preview: "", error: "" }
        }
        function watchPathCandidates(text) { return watchTreeCalls.candidates(text) }
        function addWatchPaths(lines) {
            canUndoWatch = true
            return watchTreeCalls.addPaths(lines)
        }
        // The rows' own formats by a stand-in path, as the provider keeps them by path.
        property var rowFormats: ({})
        function setWatchRowFormat(rows, format) {
            watchTreeCalls.format(rows, format)
            const next = Object.assign({}, rowFormats)
            for (const row of rows) {
                if (format === "")
                    delete next["row" + row]
                else
                    next["row" + row] = format
            }
            rowFormats = next
            return true
        }
        function watchRowFormats() { return Object.assign({}, rowFormats) }
        function setWatchRowFormats(formats) {
            rowFormats = Object.assign({}, formats)
            return true
        }
        function removeWatch(row) { watchTreeCalls.remove([row]) }
        function removeWatchRows(rows) {
            watchTreeCalls.remove(rows)
            canUndoWatch = true
            return { status: "ok", changed: rows.length }
        }
        function undoWatch() {
            watchTreeCalls.undo()
            canUndoWatch = false
            return { status: "ok", changed: 1 }
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
