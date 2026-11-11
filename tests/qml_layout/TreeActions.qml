// The endpoint tree's row actions over a nested fixture and a recording provider. MIT.
import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    width: 600
    height: 700
    visible: true
    QtObject {
        id: fake
        objectName: "treeActionsProvider"
        property var endpoints: treeActionsModel
        property var selectedEndpointIndex
        property bool watchActive: false
        property int selections: 0
        property int submissions: 0
        property var watched: []
        function selectEndpoint(index) { selectedEndpointIndex = index; ++selections; return true }
        function watchCandidate(index) { return treeActionsModel.candidate(index) }
        function addWatchMany(indexes) {
            ++submissions
            let added = 0, duplicate = 0, unsupported = 0
            const list = watched.slice()
            for (const index of indexes) {
                const path = treeActionsModel.pathOf(index)
                if (!treeActionsModel.candidate(index)) ++unsupported
                else if (list.indexOf(path) >= 0) ++duplicate
                else { list.push(path); ++added }
            }
            watched = list
            return {status: "ok", added: added, duplicate: duplicate, unsupported: unsupported, capacity: 0}
        }
    }
    EndpointTreePanel { objectName: "actionsTree"; anchors.fill: parent; provider: fake }
}
