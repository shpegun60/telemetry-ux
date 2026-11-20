// The Device tree over a real EndpointModel of the demo device with every
// kind of row: the design's checks 1-15 (DeviceTreeKinds.cpp). MIT.
import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    width: 420
    height: 760
    visible: true
    color: Theme.surfaceWindow
    EndpointTreePanel {
        objectName: "kindsTree"
        anchors.fill: parent
        framed: false
        provider: kindsProvider
    }
}
