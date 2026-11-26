import QtQuick
import QtQuick.Controls
import Telemetry.UX

ApplicationWindow {
    width: 760
    height: 520
    visible: true
    color: Theme.surfaceWindow
    // What the checks compare the rows and the header with (BA-4c).
    readonly property int rowHeight: Type.rowHeight
    readonly property color accent: Theme.accent
    QtObject {
        id: fixtureProvider
        objectName: "batchAddFixture"
        signal stateChanged()
        property int batchCount: 0
        property bool batchPending: false
        readonly property bool canExecuteBatch: batchCount > 0
        readonly property string batchStatus: "Executed at 14:02:11 - 2 of 2 done."
        property bool batchAttention: false
        readonly property bool batchSupported: true
        readonly property bool busy: false
        readonly property bool allowMutations: true
        readonly property var batchItems: batchAddItems
        // The selected item as Details shows it: nothing selected here.
        readonly property QtObject batchItem: QtObject {
            readonly property string selection: ""
            readonly property int item: 0
            readonly property string operation: ""
            readonly property bool connected: true
            readonly property bool busy: false
            readonly property bool commandSelected: false
            readonly property bool serviceSelected: false
            readonly property bool historical: false
            readonly property bool switchable: false
            readonly property bool allowMutations: true
            readonly property string serviceResponseEndpoint: ""
            readonly property var values: batchAddResponses
            readonly property var responses: batchAddResponses
            signal stateChanged()
            signal readFinished(bool published)
        }
        function executeBatch() { return false }
        function clearBatch() { return false }
        function selectBatch(row) { return false }
        function moveBatch(row, delta) { return batchAddCalls.move(row, delta) }
        function moveBatchTo(row, to) { return batchAddCalls.moveTo(row, to) }
        function removeBatch(row) { return batchAddCalls.remove(row) }
        function checkBatchPath(text) { return batchAddCalls.check(text) }
        function batchPathCandidates(text) { return batchAddCalls.candidates(text) }
        // The Device tree's drop (section 16): one item per path.
        function dropOn(dropped, target) {
            const result = batchAddCalls.addPaths(dropped.map(item => item.path))
            batchCount += result.added
            stateChanged()
            return result
        }
        function addBatchPaths(lines) {
            const result = batchAddCalls.addPaths(lines)
            batchCount += result.added
            stateChanged()
            return result
        }
    }
    // What the Device tree's drop hands over: a real JS array, as JSON.parse makes it.
    function dropFromTree() {
        return batchPanel.dropItems([{path: "drop.A"}, {path: "drop.B"}])
    }
    BatchPanel {
        id: batchPanel
        objectName: "batchAddPanel"
        anchors.fill: parent
        provider: fixtureProvider
    }
}
