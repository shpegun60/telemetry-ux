// The Device tree over a model shaped as a group tree will be (groups inside
// groups): the designer's review of 20 November 2026, 14.1 and 14.3
// (DeviceTreeGroups.cpp). MIT.
import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    width: 420
    height: 560
    visible: true
    color: Theme.surfaceWindow
    EndpointTreePanel {
        objectName: "groupsTree"
        anchors.fill: parent
        framed: false
        provider: groupsProvider
    }
}
