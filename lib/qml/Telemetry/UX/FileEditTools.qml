// Editing a file's bytes as pictograms: Edit (a switch), Save, Write to the
// device. A file the host keeps read-only (FileContent's readOnlyReason) has
// Edit off, its reason in the tooltip. Undo and Redo are the window's
// (UndoButtons over its UndoRouter), which act on the file once the keys have
// been in its Viewer.
// On the Viewer's own row, or on a host's toolbar acting on whichever Viewer
// is in front, as an editor's toolbar acts on the editor in front. MIT.
//
// `viewer` is a FileViewer, or null when no file is open: every action is
// then off. They act on that one file only - nothing else in a window keeps
// edits to save - and each tooltip names it, so a toolbar far from the
// Viewer never leaves in doubt what it saves. The actions stay in place and
// are only disabled when they cannot act, so the row never moves under the
// pointer. Edit shows the bytes (Hex) and starts editing them; pressed again
// it stops - edits not saved stay. Save writes the copy in the project; a
// device's file is changed only by Write to the device, which the host asks
// about and runs (writeRequested): it says why not in writeBlocked, and while
// the file is written (writing) nothing here edits it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
RowLayout {
    id: tools
    property var viewer: null
    // Why the copy cannot be written to the device now, in words; empty when
    // it can.
    property string writeBlocked: ""
    property bool writing: false
    signal writeRequested()
    readonly property var content: viewer ? viewer.content : null
    readonly property string fileName: viewer ? viewer.fileName : ""
    readonly property bool editing: !!content && content.editing
    readonly property bool onDevice: !!content && content.devicePath.length > 0
    // Why the file is not edited here (a file the connection makes or uses), or empty.
    readonly property string readOnly: content && content.readOnlyReason ? content.readOnlyReason : ""
    spacing: 2

    IconToolButton {
        objectName: "hexEdit"
        iconName: "edit"
        toggles: true
        switchedOn: tools.editing
        enabled: !!tools.content && tools.content.loaded && tools.content.size > 0 && !tools.writing
                 && tools.readOnly.length === 0
        disabledReason: tools.readOnly
        text: !tools.content ? qsTr("Edit the bytes of the file in the Viewer")
            : tools.editing ? qsTr("Stop editing %1").arg(tools.fileName)
            : tools.onDevice ? qsTr("Edit the bytes of %1 - the copy in the project").arg(tools.fileName)
            : qsTr("Edit the bytes of %1").arg(tools.fileName)
        onClicked: tools.editing ? tools.viewer.stopEditing() : tools.viewer.startEditing()
    }
    IconToolButton {
        objectName: "hexSave"
        iconName: "save"
        enabled: !!tools.content && tools.content.modified && !tools.writing
        text: !tools.content ? qsTr("Save the file in the Viewer")
            : tools.onDevice ? qsTr("Save %1 in the project. The device's file is unchanged.").arg(tools.fileName)
            : qsTr("Save %1 in the project").arg(tools.fileName)
        shortcutText: qsTr("Ctrl+S")
        onClicked: tools.viewer.save()
    }
    IconToolButton {
        objectName: "writeToDevice"
        iconName: "upload"
        enabled: tools.onDevice && !tools.writing && tools.writeBlocked.length === 0
        text: !tools.content ? qsTr("Write the file in the Viewer to the device")
            : tools.writing ? qsTr("Writing %1 to the device").arg(tools.fileName)
            : !tools.onDevice ? qsTr("Only a copy of a device's file can be written to the device")
            : tools.writeBlocked.length > 0 ? tools.writeBlocked
            : qsTr("Write %1 to the device…").arg(tools.fileName)
        // Never undone: Undo takes back edits of the copy only.
        note: tools.onDevice ? qsTr("Undo changes only the copy on this computer.") : ""
        onClicked: tools.writeRequested()
    }
}
