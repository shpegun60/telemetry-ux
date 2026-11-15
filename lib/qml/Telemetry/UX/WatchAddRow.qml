// The Watchlist's "+" row (WL-4b), as Live Expressions' "Add new expression":
// a path typed or pasted, completed from the device's descriptor, added on
// Enter - PathAddRow with the Watchlist's words and rules. A path the device
// lacks is kept as a row (WL-4c); one already watched shows its row. Several
// pasted lines are added at once, "path = value" lines copied from the
// Watchlist included. Presentation only: the provider resolves and adds. MIT.
import QtQuick

PathAddRow {
    id: watchAdd
    objectName: "watchAddRow"
    namePrefix: "watchAdd"
    required property var provider
    // Enter on a value already watched: show its row (a source row).
    signal reveal(int row, string notice)
    restText: qsTr("Add a path")
    placeholderText: qsTr("device.Config.Gain, data.Samples[3]")
    accessibleName: qsTr("Add a path to the Watchlist")
    accessibleDescription: qsTr("Type or paste paths, one per line. Down opens suggestions; Enter adds.")
    quietStatuses: ["adds", "watched", "missing"]
    addStatuses: ["adds", "missing"]
    typingStatuses: ["adds", "watched"]
    adapter: QtObject {
        function check(text) {
            return watchAdd.provider.checkWatchPath(text)
        }
        function candidates(text) {
            return watchAdd.provider.watchPathCandidates(text)
        }
        function enter(text) {
            if (watchAdd.check.status !== "watched")
                return false
            watchAdd.reveal(watchAdd.check.row, watchAdd.check.message)
            watchAdd.clear()
            return true
        }
        function add(lines) {
            const result = watchAdd.provider.addWatchPaths(lines)
            if (!result)
                return null
            const added = result.added || 0
            const kept = result.missing ? result.missing.length : 0
            const parts = []
            const first = lines[0].split(" = ")[0].split("\t")[0].trim()
            if (lines.length === 1 && kept === 1)
                parts.push(qsTr("Added %1, not on this device").arg(first))
            else if (added > 0)
                parts.push(lines.length === 1 && added === 1 ? qsTr("Added %1").arg(first)
                           : lines.length === 1 ? qsTr("Added %L1 values of %2").arg(added).arg(first)
                           : qsTr("Added %L1").arg(added))
            if (result.already > 0)
                parts.push(qsTr("%L1 already watched").arg(result.already))
            if (kept > 0 && lines.length > 1)
                parts.push(qsTr("%L1 not on this device").arg(kept))
            if (result.refused && result.refused.length > 0)
                parts.push(result.refused.length === 1 ? result.refused[0].reason
                                                       : qsTr("%L1 refused").arg(result.refused.length))
            if (result.status === "capacity" && result.message)
                parts.push(result.message)
            if (result.status !== "ok" && result.status !== "capacity")
                parts.push(result.message || qsTr("Not added: %1").arg(result.status))
            return {
                notice: parts.join(" · "),
                undoable: added > 0 || kept > 0,
                serial: typeof result.serial === "number" ? result.serial : 0,
                clear: added > 0 || kept > 0 || (result.already > 0 && parts.length === 1)
            }
        }
    }
}
