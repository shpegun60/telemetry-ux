// Two independent presentations of one native connection owner. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
import Telemetry.UX

Window {
    width: 1400; height: 820; visible: true
    color: "#11151c"
    title: "My Telemetry device · two independent panels"
    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            CheckBox {
                objectName: "configMutationsCheck"
                text: "Allow changes in configuration panel"
                palette.windowText: "#e8edf5"
                checked: configProvider.allowMutations
                onToggled: configProvider.allowMutations = checked
            }
            Configurator {
                objectName: "configPanel"
                Layout.fillWidth: true; Layout.fillHeight: true
                watchlistVisible: false
                provider: configProvider
            }
        }
        ColumnLayout {
            Layout.preferredWidth: 520; Layout.fillHeight: true
            Layout.fillWidth: false
            CheckBox {
                objectName: "valueMutationsCheck"
                text: "Allow changes in field panel"
                palette.windowText: "#e8edf5"
                checked: valueProvider.allowMutations
                onToggled: valueProvider.allowMutations = checked
            }
            FieldEditor {
                objectName: "valuePanel"
                Layout.fillWidth: true; Layout.fillHeight: true
                provider: valueProvider
            }
        }
    }
}
