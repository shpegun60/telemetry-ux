// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.Generated.device_api

ApplicationWindow {
    id: window
    visible: true
    width: 680
    height: 420
    title: "Generated telemetry QObject fixture / " + transportMode
    property string transportMode: "Native Encoded"
    property string outcome: "Waiting for the admitted fixture session"
    property string responseText: ""
    DeviceApi { id: api; objectName: "deviceApi" }
    Connections {
        target: api
        function onOperationFinished(token, result) {
            outcome = token + ": " + (result.accepted ? "accepted" : result.success ? "completed" :
                         result.errorKind + " " + result.message)
            if (result.success && result.operation === 3 && result.value && result.value.checksum !== undefined)
                responseText = "Inspect checksum: " + result.value.checksum
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        Label { text: api.ready ? "Admitted fixture session " + api.session : "No admitted session" }
        Label { text: api.device.configValid ? "Serial: " + api.device.config.serial : "Configuration cache is invalid" }
        Label { text: api.device.configValid ? "Signed offset: " + api.device.config.offset : "" }
        Label { text: api.device.configValid ? "Gain: " + api.device.config.gain : "" }
        RowLayout {
            Button { text: "Refresh"; enabled: api.ready; onClicked: api.device.refreshConfig() }
            Button {
                text: "Write cached value"
                enabled: api.ready && api.device.configValid
                onClicked: api.device.writeConfig(api.device.config)
            }
            Button { text: "Reset command"; enabled: api.ready; onClicked: api.device.reset() }
        }
        RowLayout {
            Button {
                text: "Inspect service"
                enabled: api.ready && api.device.configValid
                onClicked: api.device.inspect({ config: api.device.config, multiplier: 4 })
            }
            Button { text: "Accepted work"; enabled: api.ready; onClicked: api.edge.accept({}) }
            Button { text: "Service refusal"; enabled: api.ready; onClicked: api.edge.fail_2() }
        }
        Label { text: responseText; Layout.fillWidth: true }
        Label { text: outcome; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Item { Layout.fillHeight: true }
        Label { text: window.transportMode + "; no physical device is connected." }
    }
}
