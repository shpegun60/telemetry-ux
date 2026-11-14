// The Batch's "+" row (BA-1): a Field, a Command or a Service found by its
// name or path, completed from the device's descriptor, added on Enter as
// the next item - PathAddRow with the Batch's words and rules. A Field is
// read; a Command runs and a Service is called when they take no input; Enter
// on one that does selects it in the Device tree, to fill in Details. A name
// two endpoints share opens the suggestions to choose; a half-typed one takes
// the first. Several pasted lines are added at once, or none. Presentation
// only: the provider resolves and adds. MIT.
import QtQuick

PathAddRow {
    id: batchAdd
    objectName: "batchAddRow"
    namePrefix: "batchAdd"
    required property var provider
    // The endpoint that takes input was selected: the operation to add it with.
    signal selected(string operation)
    // The last notice was a refusal, said in the error colour.
    property bool lastRefused: false
    readonly property bool usable: provider.batchSupported === true
    restText: usable ? qsTr("Add by name or path") : qsTr("This device has no Batch")
    placeholderText: qsTr("Voltage or control.Reset")
    accessibleName: qsTr("Add to the Batch by name or path")
    accessibleDescription: qsTr("Type a name or a path, or paste several, one per line. Down opens suggestions; Enter adds.")
    quietStatuses: ["adds", "partial", "input", "choose", "busy"]
    addStatuses: ["adds"]
    hideWhileTyping: false
    popupMaxWidth: Type.bodyPixels * 40
    enabled: usable && provider.batchPending !== true
    function say(notice, refused) {
        lastRefused = refused
        batchAdd.added(notice, false)
    }
    adapter: QtObject {
        function check(text) {
            return batchAdd.provider.checkBatchPath(text)
        }
        function candidates(text) {
            return batchAdd.provider.batchPathCandidates(text)
        }
        function enter(text) {
            const status = batchAdd.check.status
            // Half typed: the first suggestion, as the line says.
            if (status === "partial") {
                batchAdd.choose(0)
                return true
            }
            if (status === "input") {
                const operation = batchAdd.check.operation
                if (batchAdd.provider.selectBatchPath(text)) {
                    batchAdd.selected(operation)
                    batchAdd.say(qsTr("Selected %1: fill it in Details, then Add selected draft.").arg(batchAdd.check.path), false)
                } else {
                    batchAdd.say(qsTr("Finish or cancel the edit in Details first, then press Enter again."), true)
                }
                return true
            }
            // Several share the name: the suggestions, the first to take.
            if (status === "choose") {
                batchAdd.offer()
                return true
            }
            return false
        }
        function add(lines) {
            const result = batchAdd.provider.addBatchPaths(lines)
            if (!result)
                return null
            const added = result.added || 0
            if (result.status === "ok" && added > 0) {
                const items = result.items || []
                const notice = added === 1 && items.length === 1
                    ? qsTr("Added %1 as item %L2.").arg(items[0].what).arg(result.first)
                    : qsTr("Added %L1 items as items %L2 to %L3.").arg(added).arg(result.first).arg(result.first + added - 1)
                batchAdd.lastRefused = false
                return { notice: notice, undoable: false, clear: true }
            }
            batchAdd.lastRefused = (result.message || "").length > 0
            return { notice: result.message || "", undoable: false, clear: false }
        }
    }
}
