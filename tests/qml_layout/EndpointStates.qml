// Narrow EndpointPanel action states with a host-owned fake provider. MIT.
import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    visible: true
    width: 360
    height: 640
    ListModel { id: emptyValues }
    // A Field's read values, flat: the rows of the family (BA-4·0b).
    ListModel {
        id: readValues
        objectName: "readValues"
        ListElement { name: "Gain"; detail: "f32"; value: "1.5"; typeText: "f32"; isAggregate: false; childCount: 0; loadedCount: 0; editable: true; editorText: "1.5"; takes: "Enter a number." }
        ListElement { name: "Counter"; detail: "u64"; value: "18446744073709551615"; typeText: "u64"; isAggregate: false; childCount: 0; loadedCount: 0; editable: true; editorText: "18446744073709551615"; takes: "Enter a number from 0 to 18,446,744,073,709,551,615." }
        ListElement { name: "Enabled"; detail: "bool"; value: "true"; typeText: "bool"; isAggregate: false; childCount: 0; loadedCount: 0; editable: true; editorText: "true"; takes: "Enter true or false." }
        ListElement { name: "Name"; detail: "u8[8]"; value: "8 children"; typeText: "u8[8]"; isAggregate: true; childCount: 8; loadedCount: 0; editable: false; editorText: ""; takes: "" }
    }
    ListModel { id: emptyResponses }
    // A Service's request of two values and its response (the Batch item's
    // pane in a narrow dock).
    ListModel {
        id: requestValues
        objectName: "requestValues"
        ListElement { name: "value"; detail: "f32"; value: "0"; typeText: "f32"; isAggregate: false; childCount: 0; loadedCount: 0; editable: true; editorText: "0"; takes: "Enter a number." }
        ListElement { name: "factor"; detail: "f32"; value: "0"; typeText: "f32"; isAggregate: false; childCount: 0; loadedCount: 0; editable: true; editorText: "0"; takes: "Enter a number." }
    }
    ListModel {
        id: responseValues
        objectName: "responseValues"
        ListElement { name: "result"; detail: "f32"; value: "0"; typeText: "f32"; isAggregate: false; childCount: 0; loadedCount: 0 }
        ListElement { name: "status"; detail: "u8"; value: "0"; typeText: "u8"; isAggregate: false; childCount: 0; loadedCount: 0 }
    }
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
