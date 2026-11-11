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
    required property var viewerContent
    property var lastSelection: []
    property var lastOpened: null
    property var lastOpenWith: null
    property string lastShown: ""
    property string lastSaveAs: ""

    DeviceFilesPanel {
        objectName: "panel"
        anchors.fill: parent
        files: root.filesModel
        transfers: root.transfersModel
        onDownloadRequested: function(selection) { root.lastSelection = selection }
        onOpenRequested: function(file) { root.lastOpened = file }
        onOpenWithRequested: function(file, view) { root.lastOpenWith = { path: file.path, view: view } }
        onShowInFolderRequested: function(localPath) { root.lastShown = localPath }
    }

    // The viewer, under the panel: the check sets its file and reads its state.
    FileViewer {
        objectName: "viewer"
        z: -1
        anchors.fill: parent
        content: root.viewerContent
        onSaveAsRequested: function(localFile) { root.lastSaveAs = localFile }
    }
}
