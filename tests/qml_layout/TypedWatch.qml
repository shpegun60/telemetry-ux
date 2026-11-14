import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

ApplicationWindow {
    width: 480
    height: 520
    visible: true
    color: Theme.surfaceWindow
    QtObject {
        id: fixtureProvider
        readonly property var watchlist: typedWatchModel
        readonly property int watchCount: 4
        readonly property bool connected: true
        readonly property bool watchActive: false
        readonly property bool watchPending: false
        readonly property bool canWriteWatch: true
        readonly property bool canAddWatch: false
        readonly property bool watchReadManySupported: true
        readonly property bool watchRequiresExplicitIndividual: false
        property bool watchIndividual: false
        property int watchIntervalMs: 1000
        readonly property int watchIntervalMinimumMs: 1
        readonly property string watchStatus: ""
        readonly property string watchWriteOutcome: ""
        function writeWatch(row, value) { typedWatchWrites.write(row, value) }
        function refreshWatch() {}
        function startWatch() {}
        function stopWatch() {}
        function clearWatch() {}
        function removeWatch(row) {}
        function addWatch() {}
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: Type.spacing
        WatchlistPanel {
            objectName: "typedWatchPanel"
            Layout.fillWidth: true
            Layout.fillHeight: true
            provider: fixtureProvider
            collapsible: false
            framed: false
        }
        Button {
            objectName: "outsideWatchEditor"
            Layout.alignment: Qt.AlignLeft
            text: qsTr("Other control")
        }
    }
}
