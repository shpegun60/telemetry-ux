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
}
