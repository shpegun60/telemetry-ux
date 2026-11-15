// The Batch's "+" row (BA-1, BA-4b): a finder. A Field, a Command or a
// Service found by its name or path, listed under the headings Fields,
// Commands and Services, completed from the device's descriptor and added on
// Enter as the next item: a Field is read (a write-only one written), a
// Command runs and a Service is called with its request at its initial values
// - zero, false, an enum's first name - set on the item afterwards. A kind
// word first ("command ", "fields ") lists that kind only. A name two
// endpoints share opens the suggestions to choose; a half-typed one takes the
// first. Several pasted lines are added at once, or none. PathAddRow with the
// Batch's words and rules. Presentation only: the provider resolves and adds.
// MIT.
import QtQuick

PathAddRow {
    id: batchAdd
    objectName: "batchAddRow"
    namePrefix: "batchAdd"
    required property var provider
    // The last notice was a refusal, said in the error colour.
    property bool lastRefused: false
    readonly property bool usable: provider.batchSupported === true
    restText: usable ? qsTr("Add a Field, Command or Service") : qsTr("This device has no Batch")
    placeholderText: qsTr("Name or path, such as Counter or control.Scale")
    accessibleName: qsTr("Add to the Batch")
    accessibleDescription: qsTr("Type a name or a path. Fields, Commands and Services are listed under their own headings; type command and a space to see one kind. Enter adds the highlighted one; set its values on the item.")
    quietStatuses: ["adds", "partial", "choose", "busy"]
    addStatuses: ["adds"]
    hideWhileTyping: false
    // Enter adds the highlighted suggestion or the best match at once; the
    // best match is highlighted as the suggestions come (BA-4b's design).
    enterAdds: true
    highlightBest: true
    openOnEmpty: false
    popupMaxWidth: Type.bodyPixels * 46
    enabled: usable && provider.batchPending !== true
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
                batchAdd.choose(batchAdd.firstPick())
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
                // One step to undo (UR-1); serial is the step's.
                return { notice: notice, undoable: true, serial: result.serial || 0, clear: true }
            }
            batchAdd.lastRefused = (result.message || "").length > 0
            return { notice: result.message || "", undoable: false, serial: 0, clear: false }
        }
    }
}
