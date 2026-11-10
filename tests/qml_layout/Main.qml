// A different host with real disconnected Session models, no network or I/O. MIT.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import Telemetry.UX

Window {
    required property var ownerProvider
    visible: true
    width: 1160
    height: 800
    Configurator {
        objectName: "layoutConfigurator"
        anchors.fill: parent
        anchors.margins: 16
        provider: ownerProvider
        headerComponent: Component {
            Rectangle { objectName: "customHeader"; implicitHeight: 38; color: "#233349"
                Label { anchors.centerIn: parent; text: "Host-owned header" }
            }
        }
        footerComponent: Component {
            Rectangle { objectName: "customFooter"; implicitHeight: 34; color: "#233349"
                Label { anchors.centerIn: parent; text: "Host-owned footer" }
            }
        }
    }
}
