// Isolated provider shared by independent views; no transport and no I/O. MIT.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

Window {
    visible: true
    width: 1000
    height: 640
    ListModel {
        id: endpoints
        ListElement { name: "Voltage"; detail: "Field"; isEndpoint: true }
        ListElement { name: "Voltage"; detail: "Command"; isEndpoint: true }
    }
    ListModel { id: values }
    // Existing custom providers are not required to implement selection identity.
    QtObject {
        id: legacy
        property var endpoints: endpoints
        function selectEndpoint(index) {}
    }
    QtObject {
        id: shared
        objectName: "standaloneProvider"
        property var endpoints: endpoints
        property var watchlist: values
        property string selection: ""
        property var selectedEndpointIndex
        property bool refuseSelection: false
        property int selections: 0
        property int starts: 0
        property int stops: 0
        property int watchCount: values.count
        property bool canAddWatch: selection.length > 0
        property bool connected: true
        property bool watchActive: false
        property bool watchPending: false
        property bool watchIndividual: false
        property bool watchReadManySupported: true
        property bool watchRequiresExplicitIndividual: false
        property int watchIntervalMs: 100
        property string watchStatus: "Ready"
        function selectEndpoint(index) {
            if (refuseSelection) return false
            selection = index.row === 0 ? "Field voltage" : "Command voltage"
            selectedEndpointIndex = index
            ++selections
            return true
        }
        function addWatch() { values.append({name: selection, text: "", status: "Pending"}); return true }
        function startWatch() { ++starts; watchActive = true }
        function stopWatch() { ++stops; watchActive = false }
        function refreshWatch() {}
        function clearWatch() { values.clear() }
        function removeWatch(index) { values.remove(index) }
    }
    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            EndpointTreePanel { objectName: "standaloneTree"; provider: shared; Layout.fillHeight: true; Layout.preferredWidth: 300 }
            EndpointTreePanel { objectName: "standaloneTree2"; provider: shared; Layout.fillHeight: true; Layout.preferredWidth: 300 }
            EndpointTreePanel { objectName: "legacyTree"; provider: legacy; Layout.fillHeight: true; Layout.preferredWidth: 180 }
            Label { objectName: "standaloneSelection"; text: shared.selection; Layout.fillWidth: true }
        }
        WatchlistPanel { objectName: "standaloneWatch"; provider: shared; Layout.fillWidth: true; Layout.preferredHeight: implicitHeight }
    }
}
