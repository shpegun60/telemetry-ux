import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    width: 600
    height: 700
    visible: true
    property var legacyProvider: legacy
    QtObject {
        id: legacy
        property var endpoints: bulkFixtureModel
        property var selectedEndpointIndex
        function selectEndpoint(index) { selectedEndpointIndex = index; return true }
    }
    QtObject {
        id: fake
        objectName: "bulkProvider"
        property var endpoints: bulkFixtureModel
        property var selectedEndpointIndex
        property bool watchActive: false
        property bool refuse: false
        property bool reportUnsupported: false
        property int selections: 0
        property int submissions: 0
        property string draft: "untouched"
        property var submitted: []
        function selectEndpoint(index) { selectedEndpointIndex = index; ++selections; return true }
        function watchCandidate(index) { return bulkFixtureModel.candidate(index) }
        function addWatchMany(indexes) {
            ++submissions
            if (refuse) return {status: "refused", added: 0, duplicate: 0, unsupported: 0, capacity: 0}
            submitted = indexes
            const unsupported = reportUnsupported ? 1 : 0
            return {status: "ok", added: indexes.length - unsupported, duplicate: 0, unsupported: unsupported, capacity: 0}
        }
    }
    EndpointTreePanel { objectName: "bulkTree"; anchors.fill: parent; provider: fake }

    // The host's history (UR-1): the fixture records its boxes there with
    // the JavaScript that takes them back.
    UndoHistory { id: boxesHistory }
    // A provider with watch boxes (watchState, setWatchMany) and a history, in
    // a window of its own so the bulk checks above keep theirs.
    QtObject {
        id: boxes
        objectName: "boxesProvider"
        property var endpoints: bulkFixtureModel
        property var selectedEndpointIndex
        property bool watchActive: false
        // The Dynamic provider's promise: changes are taken while polling.
        property bool watchChangesWhilePolling: false
        property int watchRevision: 0
        property var history: boxesHistory
        readonly property int watchLimit: 1024
        property bool full: false
        property int calls: 0
        property var watched: ({})
        function selectEndpoint(index) { selectedEndpointIndex = index; return true }
        function watchCandidate(index) { return bulkFixtureModel.candidate(index) }
        function addWatchMany(indexes) { return {status: "refused", added: 0, duplicate: 0, unsupported: 0, capacity: 0} }
        function watchState(index) {
            return bulkFixtureModel.candidate(index) ? (watched[index.row] ? Qt.Checked : Qt.Unchecked) : -1
        }
        function setWatchMany(indexes, on) {
            ++calls
            if (full && on)
                return {status: "capacity", changed: 0, unchanged: 0, fields: indexes.length, room: 0}
            const before = watched
            const next = Object.assign({}, watched)
            let changed = 0
            for (const index of indexes) {
                if (Boolean(next[index.row]) !== on) {
                    next[index.row] = on
                    ++changed
                }
            }
            watched = next
            ++watchRevision
            const after = watched
            const serial = changed > 0 && history ? scriptSteps.record(history, (on ? "Add " : "Remove ") + changed + " values", () => {
                watched = before
                ++watchRevision
            }, () => {
                watched = after
                ++watchRevision
            }) : 0
            return {status: "ok", changed: changed, unchanged: indexes.length - changed, fields: indexes.length, room: 1024,
                    serial: serial}
        }
    }
    Window {
        objectName: "boxesWindow"
        width: 400
        height: 500
        visible: true
        EndpointTreePanel { objectName: "boxTree"; anchors.fill: parent; provider: boxes }
        UndoRouter { workspace: boxesHistory }
    }
    // The same provider beside it as a second device's tree (a Compare
    // dock): no watching offered at all.
    Window {
        objectName: "compareWindow"
        width: 400
        height: 500
        visible: true
        EndpointTreePanel { objectName: "compareTree"; anchors.fill: parent; provider: boxes; watchBoxes: false }
    }
}
