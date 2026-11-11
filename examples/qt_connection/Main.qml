// Both panels follow one SDK connection through disconnect/reconnect. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Telemetry.UX
Window {
    width: 1440; height: 820; visible: true
    minimumWidth: 1240; minimumHeight: 600
    title: "Telemetry - one TCP connection, two panels"
    color: "#11151c"
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8
        RowLayout {
            TextField { id: host; text: "127.0.0.1"; placeholderText: "Reference server host" }
            SpinBox { id: port; from: 1; to: 65535; value: 5577; editable: true }
            Button {
                text: connection.connecting ? "Connecting..." : "Connect"
                enabled: !connection.connecting
                onClicked: connection.connectTcpReference(host.text, port.value)
            }
            Button { text: "Disconnect"; onClicked: connection.disconnect() }
            Button { objectName: "openTrend"; text: "Watchlist and Trend"; onClicked: trendWindow.show() }
            Label { text: connection.lastError; color: "#ecb05a"; Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillHeight: true; Layout.fillWidth: true
            ColumnLayout {
                Layout.fillHeight: true; Layout.fillWidth: true
                Layout.minimumWidth: 600; Layout.preferredWidth: 700
                CheckBox {
                    text: "Allow changes in configuration panel"; palette.windowText: "#e8edf5"
                    checked: configuration.allowMutations
                    onToggled: configuration.allowMutations = checked
                }
                Configurator {
                    objectName: "configurationPanel"
                    Layout.fillHeight: true; Layout.fillWidth: true
                    provider: configuration; watchlistVisible: false
                }
            }
            Configurator {
                objectName: "measurementPanel"
                Layout.fillHeight: true; Layout.fillWidth: true
                Layout.minimumWidth: 600; Layout.preferredWidth: 700
                provider: measurements; watchlistVisible: false
            }
        }
    }
    Window {
        id: trendWindow
        objectName: "trendWindow"
        width: 680; height: 700
        title: "Telemetry Watchlist and Trend"
        color: "#11151c"
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 8
            WatchlistPanel { Layout.fillWidth: true; Layout.preferredHeight: 220; provider: measurements }
            TrendPanel { Layout.fillWidth: true; Layout.fillHeight: true; provider: measurements; history: trendHistory }
        }
    }
}
