import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    id: host
    visible: true
    width: 550
    height: 640
    property color testHighlight: "#215a68"
    property color testHighlightedText: "#ffffff"
    property color testButtonText: "#e8edf5"
    property color testBase: "#17202b"
    property color testText: "#e8edf5"
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
    EndpointTreePanel {
        objectName: "searchTree"
        anchors.fill: parent
        provider: fake
        palette.highlight: host.testHighlight
        palette.highlightedText: host.testHighlightedText
        palette.buttonText: host.testButtonText
        palette.base: host.testBase
        palette.text: host.testText
    }
}
