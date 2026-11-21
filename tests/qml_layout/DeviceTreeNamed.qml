// The Device tree over a real EndpointModel of a group tree with named
// types (DeviceTreeNamed.cpp): nested groups, scoped type names, the
// ceilings and the drops' picking, keys and badge. MIT.
import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    width: 420
    height: 760
    visible: true
    color: Theme.surfaceWindow
    EndpointTreePanel {
        objectName: "namedTree"
        anchors.fill: parent
        framed: false
        provider: namedProvider
    }
}
