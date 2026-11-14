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
        readonly property string batchResponseEndpoint: ""
        readonly property bool busy: false
        readonly property var batchItems: batchAddItems
        readonly property var batchResponses: batchAddResponses
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
