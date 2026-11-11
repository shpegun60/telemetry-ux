// Narrow standalone dock: real controls with a deterministic, non-I/O provider. MIT.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import Telemetry.UX

Window {
    visible: true
    width: 320
    height: 240
    ListModel { id: values }
    QtObject {
        id: source
        objectName: "narrowProvider"
        property var watchlist: values
        property int watchCount: values.count
        property bool canAddWatch: true
        property bool connected: true
        property bool watchActive: false
        property bool watchPending: false
        property bool watchIndividual: false
        property bool watchReadManySupported: true
        property bool watchRequiresExplicitIndividual: false
        property int watchIntervalMs: 100
        property string watchStatus: "Ready"
        property int starts: 0
        property int stops: 0
        property int reads: 0
        // Row writes: offered for rows whose `writable` role is true.
        property bool canWriteWatch: true
        property string watchWriteOutcome: ""
        property var writes: []
        function writeWatch(row, text) {
            writes = writes.concat([row + ":" + text])
            watchWriteOutcome = "Writing " + values.get(row).name
            return true
        }
        function addWatch() { values.append({name: "measure.Voltage", text: "18446744073709551615", status: "SubmissionRefused", writable: false}); return true }
        function populate(count) {
            values.clear()
            for (let i = 0; i < count; ++i)
                values.append({name: "measure.Field" + i, text: "18446744073709551615", status: i === count - 1 ? "SubmissionRefusedBecauseTheDeviceIsTemporarilyUnavailable" : "SubmissionRefused", writable: i === 0})
        }
        function startWatch() { ++starts; watchActive = true }
        function stopWatch() { ++stops; watchActive = false }
        function refreshWatch() { ++reads }
        function clearWatch() { values.clear() }
        function removeWatch(index) { values.remove(index) }
    }
    WatchlistPanel {
        objectName: "narrowWatch"
        anchors.fill: parent
        provider: source
    }
}
