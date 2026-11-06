// Two independent presentations of one native connection owner. MIT.
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import Telemetry.UX

Window {
    width: 1400; height: 820; visible: true
    title: "My Telemetry device · two independent panels"
    RowLayout {
        anchors.fill: parent
        Configurator {
            Layout.fillWidth: true; Layout.fillHeight: true
            watchlistVisible: false
            provider: configProvider
        }
        FieldEditor {
            Layout.preferredWidth: 520; Layout.fillHeight: true
            provider: valueProvider
        }
    }
}
