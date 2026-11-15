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
        property bool canUndoWatch: false
        property string watchFormat: "original"
        // As the provider's: moves with every change of the rows or a format.
        property int watchRevision: 0
        // A reset's formats, while Undo can take it back.
        property var formatUndo: null
        property bool resetting: false
        // A format chosen ends what Undo could take back of a reset.
        function formatChanged() {
            ++watchRevision
            if (!resetting && formatUndo !== null) {
                formatUndo = null
                canUndoWatch = false
            }
        }
        onWatchFormatChanged: formatChanged()
        function checkWatchPath(text) { return watchTreeCalls.check(text) }
        function watchEditPreview(row, text) {
            return text === "2" ? { preview: "= 0x40000000", error: "" }
                 : text === "x" ? { preview: "", error: "x is not a number a f32 holds." }
                 : { preview: "", error: "" }
        }
        function watchPathCandidates(text) { return watchTreeCalls.candidates(text) }
        function addWatchPaths(lines) {
            formatUndo = null
            canUndoWatch = true
            ++watchRevision
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
            const undo = { format: watchFormat, rows: Object.assign({}, rowFormats) }
            resetting = true
            rowFormats = ({})
            watchFormat = "original"
            resetting = false
            formatUndo = undo
            canUndoWatch = true
            ++watchRevision
            return { status: "ok", rows: rows }
        }
        function removeWatch(row) { watchTreeCalls.remove([row]) }
        function removeWatchRows(rows) {
            watchTreeCalls.remove(rows)
            formatUndo = null
            // As the provider's: no Undo without a device to put them back against.
            canUndoWatch = connected
            ++watchRevision
            return { status: "ok", changed: rows.length }
        }
        function undoWatch() {
            if (formatUndo !== null) {
                const undo = formatUndo
                resetting = true
                rowFormats = undo.rows
                watchFormat = undo.format
                resetting = false
                formatUndo = null
                canUndoWatch = false
                ++watchRevision
                return { status: "ok", changed: Object.keys(undo.rows).length + 1 }
            }
            watchTreeCalls.undo()
            canUndoWatch = false
            ++watchRevision
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
