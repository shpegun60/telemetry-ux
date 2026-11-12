// Editing a file's bytes as pictograms: Edit (a switch), Save, Write to the
// device, Undo, Redo.
// On the Viewer's own row, or on a host's toolbar acting on whichever Viewer
// is in front, as an editor's toolbar acts on the editor in front. MIT.
//
// `viewer` is a FileViewer, or null when no file is open: every action is
// then off. They act on that one file only - nothing else in a window keeps
// edits to save - and each tooltip names it, so a toolbar far from the
// Viewer never leaves in doubt what it saves. The actions stay in place and
// are only disabled when they cannot act, so the row never moves under the
// pointer. Edit shows the bytes (Hex) and starts editing them; pressed again
// it stops - edits not saved stay. Save writes the copy on this computer; a
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
    spacing: 2

    IconToolButton {
        objectName: "hexEdit"
        iconName: "edit"
        toggles: true
        switchedOn: tools.editing
        enabled: !!tools.content && tools.content.loaded && tools.content.size > 0 && !tools.writing
        text: !tools.content ? qsTr("Edit the bytes of the file in the Viewer")
            : tools.editing ? qsTr("Stop editing %1").arg(tools.fileName)
            : tools.onDevice ? qsTr("Edit the bytes of %1 - the copy on this computer").arg(tools.fileName)
            : qsTr("Edit the bytes of %1").arg(tools.fileName)
        onClicked: tools.editing ? tools.viewer.stopEditing() : tools.viewer.startEditing()
    }
    IconToolButton {
        objectName: "hexSave"
        iconName: "save"
        enabled: !!tools.content && tools.content.modified && !tools.writing
        text: !tools.content ? qsTr("Save the file in the Viewer")
            : tools.onDevice ? qsTr("Save %1 to the copy on this computer; the device's file is unchanged").arg(tools.fileName)
            : qsTr("Save %1").arg(tools.fileName)
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
        onClicked: tools.writeRequested()
    }
    ToolSeparator {}
    IconToolButton {
        objectName: "hexUndo"
        iconName: "undo"
        enabled: !!tools.content && tools.content.canUndo && !tools.writing
        text: tools.content ? qsTr("Undo in %1").arg(tools.fileName) : qsTr("Undo")
        shortcutText: qsTr("Ctrl+Z in the bytes")
        onClicked: tools.content.undo()
    }
    IconToolButton {
        objectName: "hexRedo"
        iconName: "redo"
        enabled: !!tools.content && tools.content.canRedo && !tools.writing
        text: tools.content ? qsTr("Redo in %1").arg(tools.fileName) : qsTr("Redo")
        shortcutText: qsTr("Ctrl+Y in the bytes")
        onClicked: tools.content.redo()
    }
}
