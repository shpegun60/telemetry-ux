// Literal schema/provider strings, without an SDK or device. MIT.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import Telemetry.UX

Window {
    id: host
    required property string imageBase
    property bool unsafeControl: false
    visible: true
    width: 1280
    height: 1000

    function hostile(tag) {
        return "<b>marker-" + tag + "</b><img src=\"" + imageBase + "/" + tag + ".png\">"
    }

    QtObject {
        id: owner
        objectName: "literalOwner"
        property ListModel endpoints: ListModel {}
        property ListModel values: ListModel {}
        property ListModel responses: ListModel {}
        property ListModel watchlist: ListModel {}
        property string selection: host.hostile("selection")
        property string serviceResponseEndpoint: host.hostile("responseHeader")
        property bool connected: false
        property bool busy: false
        property bool editing: false
        property bool canEdit: false
        property bool canApply: false
        property bool canRefresh: false
        property bool commandSelected: false
        property bool canExecute: false
        property bool serviceSelected: false
        property bool canCallService: false
        property bool watchActive: false
        property bool watchPending: false
        property bool canAddWatch: false
        property int watchCount: 1
        property string watchStatus: host.hostile("watchSummary")
        property string writeOutcome: host.hostile("writeOutcome")
        property string commandOutcome: host.hostile("commandOutcome")
        property string serviceOutcome: host.hostile("serviceOutcome")
        property string statusText: host.hostile("status")
        property bool watchIndividual: false
        property int watchIntervalMs: 1000
        signal readFinished(bool published)

        function selectEndpoint(index) { return false }
        function refresh() { return false }
        function beginEdit() { return false }
        function cancelEdit() {}
        function apply() { return false }
        function executeCommand() { return false }
        function callService() { return false }
        function setScalarText(index, text) { return false }
        function addWatch() { return false }
        function removeWatch(index) { return false }
        function clearWatch() {}
        function refreshWatch() { return false }
        function startWatch() { return false }
        function stopWatch() {}
        function publishTrendSample() {
            watchlist.setProperty(0, "sampleText", host.hostile("trendValue"))
            watchlist.setProperty(0, "sampleRevision", "2")
            watchlist.setProperty(0, "status", "Ok")
        }

        Component.onCompleted: {
            endpoints.append({name: host.hostile("endpoint"), detail: host.hostile("endpointTip"), isEndpoint: true})
            values.append({name: host.hostile("member"), detail: host.hostile("valueTip"), value: host.hostile("enum"), isAggregate: false, childCount: 0, loadedCount: 0, editable: false, editorText: "0"})
            responses.append({name: host.hostile("responseMember"), detail: host.hostile("responseTip"), value: host.hostile("responseEnum"), isAggregate: false, childCount: 0, loadedCount: 0})
            watchlist.append({name: host.hostile("watchName"), status: host.hostile("watchStatus"), text: host.hostile("watchValue"), sampleText: "", sampleRevision: "1", sampleEpoch: "1"})
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: host.unsafeControl ? unsafeLabel : safeView
    }
    Component {
        id: unsafeLabel
        Label { objectName: "unsafeLabel"; text: host.hostile("unsafe") }
    }
    Component {
        id: safeView
        Configurator { objectName: "literalView"; provider: owner }
    }
}
