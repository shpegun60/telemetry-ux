// The Batch's "+" row (BA-1): a Field, a Command or a Service found by its
// name or path, completed from the device's descriptor, added on Enter as
// the next item - PathAddRow with the Batch's words and rules. A Field is
// read; a Command runs and a Service is called when they take no input; Enter
// on one that does selects it in the Device tree, to fill in Details. A name
// two endpoints share opens the suggestions to choose. Several pasted lines
// are added at once, or none. Presentation only: the provider resolves and
// adds. MIT.
import QtQuick

PathAddRow {
    id: batchAdd
    objectName: "batchAddRow"
    namePrefix: "batchAdd"
    required property var provider
    restText: qsTr("Add by name or path")
    placeholderText: qsTr("Voltage, control.Reset, device.Config")
    accessibleName: qsTr("Add to the Batch by name or path")
    accessibleDescription: qsTr("Type a name or a path, or paste several, one per line. Down opens suggestions; Enter adds.")
    quietStatuses: ["adds", "input", "choose"]
    addStatuses: ["adds"]
    typingStatuses: ["adds"]
    enabled: provider.batchPending !== true
    adapter: QtObject {
        function check(text) {
            return batchAdd.provider.checkBatchPath(text)
        }
        function candidates(text) {
            return batchAdd.provider.batchPathCandidates(text)
        }
        function enter(text) {
            if (batchAdd.check.status === "input") {
                if (batchAdd.provider.selectBatchPath(text))
                    batchAdd.added(qsTr("Selected %1: fill it in Details, then Add selected.").arg(batchAdd.check.path), false)
                return true
            }
            // Several share the name: the suggestions, the first to take.
            if (batchAdd.check.status === "choose")
                return batchAdd.offer() || true
            return false
        }
        function add(lines) {
            const before = batchAdd.provider.batchCount
            const result = batchAdd.provider.addBatchPaths(lines)
            if (!result)
                return null
            const added = result.added || 0
            if (result.status === "ok" && added > 0) {
                const first = batchAdd.check.path && batchAdd.check.path.length > 0 ? batchAdd.check.path : lines[0]
                return {
                    notice: added === 1 ? qsTr("Added %1 as item %L2").arg(first).arg(before + 1)
                                        : qsTr("Added %L1 items, %L2 to %L3").arg(added).arg(before + 1).arg(before + added),
                    undoable: false,
                    clear: true
                }
            }
            return { notice: result.message || "", undoable: false, clear: false }
        }
    }
}
