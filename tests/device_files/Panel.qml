// The Device files panel as a host places it, for the panel check. MIT.
import QtQuick
import QtQuick.Controls
import Telemetry.UX

ApplicationWindow {
    id: root
    width: 420
    height: 600
    visible: true

    required property var filesModel
    required property var transfersModel
    property var lastSelection: []
    property var lastOpened: null

    DeviceFilesPanel {
        objectName: "panel"
        anchors.fill: parent
        files: root.filesModel
        transfers: root.transfersModel
        onDownloadRequested: function(selection) { root.lastSelection = selection }
        onOpenRequested: function(file) { root.lastOpened = file }
    }
}
