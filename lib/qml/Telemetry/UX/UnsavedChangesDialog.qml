// Asks before edits not saved are lost - on Reload, closing a file, quitting:
// Save, Discard or Cancel. A host calls ask(names, save, discard, then): the
// names of the files with changes, save() writing them (empty when written,
// else why not), discard() dropping them, then() what was asked for, run once
// the changes are saved or discarded. A save that fails says why and asks
// again; Cancel does nothing. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "unsavedChangesDialog"

    property var names: []
    property var saveAction: null
    property var discardAction: null
    property var thenAction: null
    property string problem: ""

    function ask(names, save, discard, then) {
        if (!names || names.length === 0) {
            then()
            return
        }
        dialog.names = names
        dialog.saveAction = save
        dialog.discardAction = discard
        dialog.thenAction = then
        dialog.problem = ""
        open()
    }
    function finish() {
        const then = dialog.thenAction
        dialog.thenAction = null
        close()
        if (then)
            then()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent ? parent.width - 32 : 520)
    modal: true
    title: names.length === 1 ? qsTr("Save the changes to %1?").arg(names[0]) : qsTr("Save the changes?")
    standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel

    ColumnLayout {
        width: parent.width
        spacing: 8
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: dialog.names.length === 1
                  ? qsTr("Its edited bytes are not saved to the copy on this computer; without saving they are lost. A device's file is not changed either way.")
                  : qsTr("%n files have edited bytes not saved to their copies on this computer: %1. Without saving they are lost. A device's file is not changed either way.", "", dialog.names.length)
                        .arg(dialog.names.join(", "))
        }
        Label {
            objectName: "unsavedChangesProblem"
            visible: dialog.problem.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.error
            text: dialog.problem
            Accessible.role: Accessible.AlertMessage
        }
    }

    // Save is the default: Enter saves, Escape cancels, Discard is never the easy press.
    onOpened: {
        const save = standardButton(Dialog.Save)
        if (save)
            save.forceActiveFocus()
    }
    onAccepted: {
        const problem = dialog.saveAction ? dialog.saveAction() : ""
        if (problem && problem.length > 0) {
            dialog.problem = problem
            // Accept closes the dialog after this handler: open it again after that.
            Qt.callLater(dialog.open)
            return
        }
        finish()
    }
    onDiscarded: {
        if (dialog.discardAction)
            dialog.discardAction()
        finish()
    }
    onRejected: dialog.thenAction = null
}
