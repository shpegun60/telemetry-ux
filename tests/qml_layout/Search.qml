import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    visible: true
    width: 550
    height: 640
    QtObject {
        id: fake
        objectName: "searchProvider"
        property var endpoints: searchFixtureModel
        property var selectedEndpointIndex
        property bool refuse: false
        property int calls: 0
        function selectEndpoint(index) {
            if (refuse) return false
            selectedEndpointIndex = index
            ++calls
            return true
        }
    }
    EndpointTreePanel { objectName: "searchTree"; anchors.fill: parent; provider: fake }
}
