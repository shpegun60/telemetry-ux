// Narrow EndpointPanel action states with a host-owned fake provider. MIT.
import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    visible: true
    width: 360
    height: 640
    ListModel { id: emptyValues }
    ListModel { id: emptyResponses }
    QtObject {
        id: fake
        objectName: "endpointStateProvider"
        property string selection: "measure.A_very_long_endpoint_name_for_layout_testing"
        property bool busy: false
        property bool editing: false
        property bool commandSelected: false
        property bool serviceSelected: false
        property bool canEdit: true
        property bool canApply: true
        property bool canExecute: true
        property bool canCallService: true
        property bool canRefresh: true
        property string serviceResponseEndpoint: ""
        property string writeOutcome: ""
        property string commandOutcome: ""
        property string serviceOutcome: ""
        property var values: emptyValues
        property var responses: emptyResponses
        property int edits: 0
        property int cancels: 0
        property int applies: 0
        property int refreshes: 0
        property int commands: 0
        property int services: 0
        signal readFinished(bool published)
        function beginEdit() { ++edits; editing = true }
        function cancelEdit() { ++cancels; editing = false }
        function apply() { ++applies }
        function refresh() { ++refreshes }
        function executeCommand() { ++commands }
        function callService() { ++services }
    }
    EndpointPanel {
        objectName: "endpointStatePanel"
        anchors.fill: parent
        provider: fake
    }
}
