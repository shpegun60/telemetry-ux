// Programmer-review downstream window: four ready-made panels over one provider.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

ApplicationWindow {
    id: window
    width: 1400; height: 860; visible: true
    title: "MyDevice panel (programmer review)"

    // Walk the provider's endpoint model and collect every watchable Field
    // whose path starts with the given group. Returns the addWatchMany() result.
    function watchGroup(group) {
        const model = device.endpoints
        const found = []
        function walk(parent, path) {
            for (let row = 0; row < model.rowCount(parent); ++row) {
                const index = model.index(row, 0, parent)
                const name = model.data(index) // DisplayRole == "name" (found in DynamicModels.cpp)
                const full = path.length ? path + "." + name : name
                if (device.watchCandidate(index) && full.indexOf(group + ".") >= 0)
                    found.push(index)
                walk(index, full)
            }
        }
        walk(undefined, "")
        return device.addWatchMany(found)
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            TextField { id: host; text: "127.0.0.1" }
            SpinBox { id: port; from: 1; to: 65535; value: startupPort; editable: true }
            Button {
                text: connection.connecting ? "Connecting..." : "Connect"
                enabled: !connection.ready && !connection.connecting
                onClicked: connection.connectTcpReference(host.text, port.value)
            }
            Button { text: "Disconnect"; onClicked: connection.disconnect() }
            Button {
                text: "Watch all measure.*"
                enabled: connection.ready
                onClicked: status.text = JSON.stringify(window.watchGroup("measure"))
            }
            CheckBox {
                text: "Allow changes"
                checked: device.allowMutations
                onToggled: device.allowMutations = checked
            }
            Label { id: status; text: connection.lastError; Layout.fillWidth: true; elide: Text.ElideRight }
        }
    }

    SplitView {
        anchors.fill: parent
        EndpointTreePanel { provider: device; SplitView.preferredWidth: 320 }
        EndpointPanel { provider: device; SplitView.fillWidth: true }
        SplitView {
            orientation: Qt.Vertical
            SplitView.preferredWidth: 460
            WatchlistPanel { provider: device; SplitView.preferredHeight: 360 }
            TrendPanel { provider: device; history: trendHistory; SplitView.fillHeight: true }
        }
    }
}
