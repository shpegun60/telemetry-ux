import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    required property var ownerProvider
    visible: true
    width: 550
    height: 640
    EndpointTreePanel { objectName: "searchTree"; anchors.fill: parent; provider: ownerProvider }
}
