// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

ApplicationWindow {
    id: window
    objectName: "inspectorWindow"
    visible: true
    width: 1080
    height: 720
    minimumWidth: 780
    minimumHeight: 480
    title: "Telemetry Inspector"
    color: "#11151c"
    palette.window: "#11151c"
    palette.windowText: "#e8edf5"
    palette.base: "#17202b"
    palette.alternateBase: "#1c2836"
    palette.text: "#e8edf5"
    palette.button: "#233349"
    palette.buttonText: "#e8edf5"
    palette.highlight: "#215a68"
    palette.highlightedText: "#ffffff"
    palette.placeholderText: "#718297"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Label { text: "TELEMETRY"; color: "#68d6c2"; font.pixelSize: 12; font.letterSpacing: 2 }
                Label { text: "Universal Inspector"; font.pixelSize: 28; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            Label { text: controller.editing || controller.commandSelected || controller.serviceSelected ? "LOCAL DRAFT" : controller.writesEnabled ? "CHANGES ENABLED" : "READ ONLY"; color: "#99abbf"; font.pixelSize: 12 }
            Button { text: "Demo device"; onClicked: controller.connectDemo() }
            Button { objectName: "writableDemoButton"; text: "Writable demo"; onClicked: controller.connectWritableDemo() }
            Button { text: "Disconnect"; enabled: controller.connected || controller.busy; onClicked: controller.disconnectDevice() }
        }
        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                text: "Enable Field writes, Commands and Services for this connection"
                checked: controller.writesEnabled
                enabled: controller.writeProfileSupported && !controller.busy
                onToggled: controller.writesEnabled = checked
            }
            Button {
                objectName: "readManyDemoButton"
                text: "ReadMany demo"
                onClicked: controller.connectReadManyDemo()
            }
            Label {
                visible: controller.connected && !controller.writeProfileSupported
                text: "This transport profile is read-only."
                color: "#99abbf"
                Layout.fillWidth: true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: tcpHost
                objectName: "tcpHost"
                Layout.fillWidth: true
                text: controller.tcpHost
                placeholderText: "TCP host"
                onAccepted: controller.connectTcp(text, tcpPort.value)
            }
            SpinBox {
                id: tcpPort
                objectName: "tcpPort"
                from: 1
                to: 65535
                value: controller.tcpPort
                editable: true
            }
            Button {
                text: "Connect TCP"
                enabled: tcpHost.text.length > 0 && !controller.busy
                onClicked: controller.connectTcp(tcpHost.text, tcpPort.value)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: controller.serialAvailable
            ComboBox {
                id: serialPort
                objectName: "serialPort"
                Layout.fillWidth: true
                editable: true
                model: controller.serialPorts()
                editText: controller.serialPort
            }
            Button {
                text: "Scan ports"
                onClicked: serialPort.model = controller.serialPorts()
            }
            SpinBox {
                id: serialBaud
                from: 1200
                to: 4000000
                value: controller.serialBaud
                editable: true
            }
            Button {
                text: "Connect UART"
                enabled: serialPort.editText.length > 0 && !controller.busy
                onClicked: controller.connectSerial(serialPort.editText, serialBaud.value)
            }
        }
        CheckBox {
            objectName: "readManyDiscoveryCheck"
            visible: controller.serialAvailable
            text: "Discover ReadMany on the next UART connection"
            checked: controller.readManyDiscovery
            onToggled: controller.readManyDiscovery = checked
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: descriptorPath
                objectName: "descriptorPath"
                Layout.fillWidth: true
                placeholderText: "Descriptor file path · metadata only"
                onAccepted: controller.openDescriptorPath(text)
            }
            Button {
                text: "Open Descriptor"
                enabled: descriptorPath.text.length > 0
                onClicked: controller.openDescriptorPath(descriptorPath.text)
            }
        }
        Configurator {
            objectName: "configuratorView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            provider: controller
        }
        Label {
            text: controller.serialAvailable
                ? "Connect TCP or UART to read a device. Demo device uses simulated values."
                : "Connect TCP to read a device. Demo device uses simulated values."
            color: "#718297"
            font.pixelSize: 11
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
    }
}
