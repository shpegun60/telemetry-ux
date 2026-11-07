// Both panels follow one SDK connection through disconnect/reconnect. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Telemetry.UX
Window {
    width: 1440; height: 820; visible: true
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
            Label { text: connection.lastError; color: "#ecb05a"; Layout.fillWidth: true }
        }
        RowLayout {
            Layout.fillHeight: true; Layout.fillWidth: true
            ColumnLayout {
                Layout.fillHeight: true; Layout.fillWidth: true
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
            FieldEditor {
                objectName: "measurementPanel"
                Layout.fillHeight: true; Layout.preferredWidth: 520
                provider: measurements
            }
        }
    }
}
