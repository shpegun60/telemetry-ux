import QtQuick
import QtQuick.Controls
import Telemetry.UX

ApplicationWindow {
    width: 760
    height: 520
    visible: true
    color: Theme.surfaceWindow
    QtObject {
        id: fixtureProvider
        objectName: "batchAddFixture"
        signal stateChanged()
        property int batchCount: 0
        property bool batchPending: false
        readonly property bool canExecuteBatch: batchCount > 0
        readonly property string batchStatus: ""
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
        function canAddBatch(operation) { return false }
        function addBatch(operation) { return false }
        function executeBatch() { return false }
        function clearBatch() { return false }
        function selectBatch(row) { return false }
        function moveBatch(row, delta) { return false }
        function removeBatch(row) { return false }
        function checkBatchPath(text) { return batchAddCalls.check(text) }
        function batchPathCandidates(text) { return batchAddCalls.candidates(text) }
        function addBatchPaths(lines) {
            const result = batchAddCalls.addPaths(lines)
            batchCount += result.added
            stateChanged()
            return result
        }
        function selectBatchPath(text) { return batchAddCalls.selectPath(text) }
    }
    BatchPanel {
        objectName: "batchAddPanel"
        anchors.fill: parent
        provider: fixtureProvider
    }
}
