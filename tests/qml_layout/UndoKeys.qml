// One window with every kind of place Ctrl+Z can be pressed (UR-1): the real
// Configurator over a disconnected Session, a file in a Viewer, plain
// controls, a text field, an inert area, a menu and a modal dialog. MIT.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

Window {
    id: host
    required property var ownerProvider
    required property var fileContent
    visible: true
    width: 1300
    height: 900
    color: Theme.surfaceWindow
    UndoHistory { id: workspace; objectName: "workspace" }
    UndoRouter { id: router; objectName: "router"; workspace: workspace }
    Component.onCompleted: ownerProvider.history = workspace
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        RowLayout {
            Layout.fillWidth: true
            UndoButtons { objectName: "undoButtons"; router: router }
            TextField { objectName: "plainField"; text: "typed"; Layout.preferredWidth: 120 }
            Button { objectName: "plainButton"; text: "Plain" }
            // A notice that speaks for the test's own steps, wherever undone.
            UndoNotice {
                objectName: "keysNotice"
                Layout.fillWidth: true
                history: workspace
                kinds: ["test.script"]
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Configurator {
                objectName: "configurator"
                Layout.fillWidth: true
                Layout.fillHeight: true
                provider: host.ownerProvider
            }
            ColumnLayout {
                Layout.preferredWidth: 380
                Layout.fillHeight: true
                // Details as a host holds them: Ctrl+Z refused in words.
                EndpointPanel {
                    id: details
                    objectName: "details"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 260
                    provider: host.ownerProvider
                    UndoScope.refusal: details.undoRefusal
                }
                FileViewer {
                    objectName: "viewer"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    content: host.fileContent
                    mode: "hex"
                }
                // A second device's area: Ctrl+Z does nothing here.
                Pane {
                    objectName: "inertArea"
                    Layout.fillWidth: true
                    UndoScope.inert: true
                    RowLayout {
                        Button { objectName: "inertButton"; text: "Second device" }
                        TextField { objectName: "inertField"; text: "x"; Layout.preferredWidth: 80 }
                    }
                }
            }
        }
    }
    Menu {
        id: menu
        objectName: "testMenu"
        MenuItem { objectName: "testMenuItem"; text: "One" }
    }
    // A dock floated out of the window: a window it owns.
    Window {
        objectName: "floatingDock"
        transientParent: host
        width: 300
        height: 120
        color: Theme.surfaceWindow
        RowLayout {
            anchors.fill: parent
            Button { objectName: "floatingButton"; text: "Floating" }
            Pane {
                UndoScope.inert: true
                Button { objectName: "floatingInertButton"; text: "Second device" }
            }
        }
    }
    Dialog {
        objectName: "modalDialog"
        modal: true
        anchors.centerIn: parent
        title: "A question"
        standardButtons: Dialog.Ok | Dialog.Cancel
    }
}
