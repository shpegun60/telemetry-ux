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

    // A provider with watch boxes (watchState, setWatchMany, undoWatch), in a
    // window of its own so the bulk checks above keep theirs.
    QtObject {
        id: boxes
        objectName: "boxesProvider"
        property var endpoints: bulkFixtureModel
        property var selectedEndpointIndex
        property bool watchActive: false
        property int watchRevision: 0
        property bool canUndoWatch: false
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
            const next = Object.assign({}, watched)
            let changed = 0
            for (const index of indexes) {
                if (Boolean(next[index.row]) !== on) {
                    next[index.row] = on
                    ++changed
                }
            }
            watched = next
            canUndoWatch = changed > 0
            ++watchRevision
            return {status: "ok", changed: changed, unchanged: indexes.length - changed, fields: indexes.length, room: 1024}
        }
        function undoWatch() { canUndoWatch = false; watched = ({}); ++watchRevision; return {status: "ok", changed: 1} }
    }
    Window {
        objectName: "boxesWindow"
        width: 400
        height: 500
        visible: true
        EndpointTreePanel { objectName: "boxTree"; anchors.fill: parent; provider: boxes }
    }
}
