// Independent host chrome with an explicit borrowed provider. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

ApplicationWindow {
    id: host
    objectName: "embeddingHost"
    required property var deviceProvider
    property bool viewLoaded: true
    visible: true
    width: 1280
    height: 960
    title: "My application dashboard"
    color: "#f0f2f5"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Label {
            objectName: "hostOnlyLabel"
            text: "My dashboard: a reusable Configurator view"
            color: "#17202b"
        }
        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: host.viewLoaded
            sourceComponent: Configurator {
                objectName: "embeddedConfigurator"
                provider: host.deviceProvider
            }
        }
        Label {
            text: "The host owns device connections and permissions."
            color: "#17202b"
        }
    }
}
