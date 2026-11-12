// One file in the viewer: its format's own view when a codec read it, a table
// when it is CSV, text when it is text, hex always, with a switch between the
// ones it has. Hex edits the bytes once Edit is on; Reload asks first when
// edits are not saved. MIT.
//
// Editing's actions - Edit, Save, Undo, Redo, as pictograms (FileEditTools) -
// are on the viewer's top row; a host with a toolbar of its own sets
// editToolsShown to false and puts a FileEditTools there for the viewer in
// front, as an editor's toolbar acts on the editor in front. Either calls
// startEditing(), stopEditing(), save(); askDiscard() and saveEditedCopy()
// are also in the `⋯` menu. A host that offers the file's actions elsewhere -
// a tab's right-click menu - sets menuButtonShown to false; the top row then
// appears only for a file with more than one view, to switch between them.
//
// The host gives `content`, a FileContent - usually a row of OpenFiles, whose
// role of that name fills it in a view's delegate - and answers the requests:
// reloadRequested() to read the device's file again, cancelRequested() while
// it is being read, openExternallyRequested, showInFolderRequested and
// saveAsRequested with the local file, closeRequested. The file's actions are
// in its `⋯` menu (and the host's tab menu), not in a row of buttons.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import Telemetry.UX

Pane {
    id: viewer
    objectName: "fileViewer"
    padding: 0

    required property var content
    // The host can read the device's file again (connected, the file listed).
    property bool reloadable: false
    // "decoded", "table", "text" or "hex"; empty for the format's own first choice.
    property string mode: ""
    // Editing's pictograms on the top row; false when the host has them on its toolbar.
    property bool editToolsShown: true
    // The `⋯` button on the top row; false when the host has one of its own.
    property bool menuButtonShown: true
    readonly property bool ready: !!content && content.loaded
    readonly property bool reading: !!content && content.loading
    readonly property string problem: content ? content.error : ""
    // Nothing is shown until the file has been read.
    readonly property string shownMode: !ready ? "" : offers(mode) ? mode
        : content.decoded ? "decoded" : content.tabular ? "table" : content.textual ? "text" : "hex"
    readonly property int modeCount: (offers("decoded") ? 1 : 0) + (offers("table") ? 1 : 0)
        + (offers("text") ? 1 : 0) + (offers("hex") ? 1 : 0)
    // The device's name, or the local file's for one from disk.
    readonly property string fileName: {
        if (!content)
            return ""
        const path = content.devicePath.length > 0 ? content.devicePath : content.localFile
        return path.substring(path.lastIndexOf("/") + 1)
    }

    signal reloadRequested()
    signal cancelRequested()
    signal openExternallyRequested(string localFile)
    signal showInFolderRequested(string localFile)
    signal saveAsRequested(string localFile)
    signal closeRequested()

    // The device's path, or the local file's for one from disk.
    readonly property string shownPath: !content ? ""
        : content.devicePath.length > 0 ? content.devicePath : content.localFile
    function openMenu() { moreMenu.popup(moreButton, 0, moreButton.height) }
    // Editing, from the row, a toolbar or a menu. Edit shows the bytes first.
    function startEditing() {
        if (!viewer.ready || viewer.content.size === 0)
            return
        viewer.mode = "hex"
        hexPane.startEditing()
    }
    function stopEditing() { hexPane.stopEditing() }
    // Empty when written, else why not - said on the status line, in Hex.
    function save() {
        const problem = hexPane.save()
        if (problem.length > 0)
            viewer.mode = "hex"
        return problem
    }
    function askDiscard() { hexPane.askDiscard() }
    // Restore the device's version: asked with how many bytes go back, edits
    // not saved included; one step, so Undo takes it back.
    readonly property bool restorable: !!content && content.hasReference && content.referenceDiffers > 0
    function askRestore() {
        if (viewer.ready && viewer.restorable)
            restoreDialog.open()
    }
    // Paste over the bytes and Replace, from a menu: in Hex, while editing.
    function paste() {
        if (!viewer.ready || !viewer.content.editing)
            return ""
        viewer.mode = "hex"
        return hexPane.paste()
    }
    function openReplace() {
        if (!viewer.ready || !viewer.content.editing)
            return
        viewer.mode = "hex"
        hexPane.openReplace()
    }
    function saveEditedCopy() { hexPane.saveCopy() }
    // Reload loses edits not saved: asked first, then read again.
    function askReload() {
        if (!content || !content.modified) {
            viewer.reloadRequested()
            return
        }
        unsaved.ask([viewer.fileName], function() { return viewer.content.save() },
                    function() { viewer.content.discard() }, function() { viewer.reloadRequested() })
    }
    UnsavedChangesDialog { id: unsaved }
    Dialog {
        id: restoreDialog
        objectName: "restoreDeviceDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.max(280, Math.min(480, (parent ? parent.width : 480) - 32))
        modal: true
        title: qsTr("Restore the device's version?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string problem: ""
        ColumnLayout {
            width: parent.width
            spacing: Type.padding
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: !viewer.content ? ""
                      : viewer.content.referenceDiffers === 1
                      ? qsTr("1 byte of this copy goes back to %1, edits not saved included. Undo takes it back. Nothing is written until you save.")
                            .arg(viewer.content.referenceLabel)
                      : qsTr("%L1 bytes of this copy go back to %2, edits not saved included. Undo takes it back. Nothing is written until you save.")
                            .arg(viewer.content.referenceDiffers).arg(viewer.content.referenceLabel)
            }
            Label {
                objectName: "restoreDeviceProblem"
                visible: restoreDialog.problem.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.error
                text: restoreDialog.problem
                Accessible.role: Accessible.AlertMessage
            }
        }
        Component.onCompleted: standardButton(Dialog.Ok).text = qsTr("Restore")
        onOpened: {
            problem = ""
            standardButton(Dialog.Cancel).forceActiveFocus()
        }
        onAccepted: {
            const problem = viewer.content.restoreReference()
            if (problem.length > 0) {
                restoreDialog.problem = problem
                Qt.callLater(restoreDialog.open)
                return
            }
            viewer.mode = "hex"
            hexPane.focusContent()
        }
    }
    // Ctrl+S while the keyboard is anywhere in this viewer - Hex, Text, Table.
    Shortcut {
        sequences: [StandardKey.Save]
        enabled: viewer.visible && viewer.activeFocus && !!viewer.content && viewer.content.modified
        onActivated: viewer.save()
    }

    // The keyboard into the file: the table, the hex rows or the text, or
    // Cancel while it is being read.
    function focusContent() {
        if (reading)
            cancelLink.forceActiveFocus()
        else if (shownMode === "table")
            tablePane.focusContent()
        else if (shownMode === "hex")
            hexPane.focusContent()
        else if (shownMode === "text")
            textPane.focusContent()
        else if (shownMode === "decoded" && decodedView.item)
            decodedView.item.forceActiveFocus()
    }

    function offers(name) {
        if (!viewer.content || !viewer.content.loaded)
            return false
        return name === "hex" ? true
             : name === "decoded" ? !!viewer.content.decoded
             : name === "text" ? viewer.content.textual
             : name === "table" ? viewer.content.tabular : false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            objectName: "fileViewerTopRow"
            // Nothing to hold - one view, the actions elsewhere - takes no room.
            visible: viewer.modeCount > 1 || viewer.editToolsShown || viewer.menuButtonShown
            Layout.fillWidth: true
            Layout.margins: Type.padding
            spacing: Type.tight

            // The ways this file can be shown; hidden when there is only one.
            ButtonGroup { id: modes }
            Repeater {
                model: [
                    // The codec's name, translated where it was made.
                    { name: "decoded", text: viewer.content ? viewer.content.codecName : "" },
                    { name: "table", text: qsTr("Table") },
                    { name: "text", text: qsTr("Text") },
                    { name: "hex", text: qsTr("Hex") }
                ]
                delegate: Button {
                    required property var modelData
                    objectName: "fileViewerMode_" + modelData.name
                    visible: viewer.modeCount > 1 && viewer.offers(modelData.name)
                    text: modelData.text
                    checkable: true
                    checked: viewer.shownMode === modelData.name
                    flat: !checked
                    ButtonGroup.group: modes
                    onClicked: viewer.mode = modelData.name
                }
            }
            Item { Layout.fillWidth: true }
            // Editing the bytes, as pictograms, unless the host has them on
            // its own toolbar.
            FileEditTools {
                objectName: "fileViewerEditTools"
                visible: viewer.editToolsShown
                viewer: viewer
            }
            // The file's actions, as the tab's menu has them.
            ToolButton {
                id: moreButton
                objectName: "fileViewerMore"
                visible: viewer.menuButtonShown
                flat: true
                implicitWidth: Math.max(32, implicitContentWidth + 16)
                font.pointSize: Type.body * 1.25
                text: qsTr("⋯")
                Accessible.name: qsTr("More actions")
                Accessible.description: viewer.fileName
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: qsTr("More actions")
                onClicked: viewer.openMenu()
            }
        }

        Menu {
            id: moreMenu
            objectName: "fileViewerMenu"
            MenuItem {
                objectName: "fileViewerReload"
                text: qsTr("&Reload%1\tCtrl+R").arg(viewer.reloadable ? "" : qsTr(" (not connected)"))
                enabled: viewer.reloadable && !viewer.reading
                onTriggered: viewer.askReload()
            }
            MenuItem {
                objectName: "hexSaveCopy"
                // The bytes as edited, not yet saved, written elsewhere.
                visible: !!viewer.content && viewer.content.modified
                height: visible ? implicitHeight : 0
                text: qsTr("Save the &edited bytes as…")
                onTriggered: viewer.saveEditedCopy()
            }
            MenuItem {
                objectName: "fileViewerRestore"
                visible: viewer.restorable
                height: visible ? implicitHeight : 0
                text: qsTr("Restore the &device's version…")
                onTriggered: viewer.askRestore()
            }
            MenuItem {
                objectName: "hexDiscard"
                visible: !!viewer.content && viewer.content.modified
                height: visible ? implicitHeight : 0
                text: qsTr("&Discard the changes…")
                onTriggered: viewer.askDiscard()
            }
            MenuItem {
                objectName: "fileViewerSaveAs"
                text: qsTr("&Save as…\tCtrl+Shift+S")
                enabled: viewer.ready
                onTriggered: viewer.saveAsRequested(viewer.content.localFile)
            }
            MenuSeparator {}
            MenuItem {
                objectName: "fileViewerCopyPath"
                text: qsTr("Copy &path\tAlt+Shift+C")
                enabled: viewer.shownPath.length > 0
                onTriggered: Clipboard.setText(viewer.shownPath)
            }
            MenuItem {
                objectName: "fileViewerShow"
                text: qsTr("Show in &folder\tAlt+Shift+R")
                enabled: viewer.ready
                onTriggered: viewer.showInFolderRequested(viewer.content.localFile)
            }
            MenuItem {
                objectName: "fileViewerExternal"
                text: qsTr("Open in another pro&gram")
                enabled: viewer.ready
                onTriggered: viewer.openExternallyRequested(viewer.content.localFile)
            }
            MenuSeparator {}
            // The view's own options.
            MenuItem {
                objectName: "fileViewerWrap"
                visible: viewer.shownMode === "text"
                height: visible ? implicitHeight : 0
                text: qsTr("&Wrap lines\tAlt+Z")
                checkable: true
                checked: textPane.wrap
                onTriggered: textPane.wrap = checked
            }
            MenuSeparator {
                visible: viewer.shownMode === "text"
                height: visible ? implicitHeight : 0
            }
            MenuItem {
                objectName: "fileViewerClose"
                text: qsTr("&Close\tCtrl+W")
                onTriggered: viewer.closeRequested()
            }
        }

        // While the device's file is read: what, how far, and a way out.
        Item {
            objectName: "fileViewerReading"
            visible: viewer.reading
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height / 3 - height / 2
                width: Math.min(320, parent.width - 32)
                spacing: Type.padding

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    color: Theme.textSecondary
                    // The device's name goes in last, under the highest number.
                    text: viewer.reading ? qsTr("Reading %2… %1").arg(viewer.content.figures).arg(viewer.fileName) : ""
                }
                ProgressLine {
                    Layout.fillWidth: true
                    thickness: 6
                    progress: viewer.reading ? viewer.content.progress : -1
                    running: viewer.reading
                    description: qsTr("Reading %1").arg(viewer.fileName)
                    valueText: viewer.reading ? viewer.content.figures : ""
                }
                LinkButton {
                    id: cancelLink
                    objectName: "fileViewerCancel"
                    Layout.alignment: Qt.AlignHCenter
                    tint: Theme.textSecondary
                    text: qsTr("Cancel")
                    onClicked: viewer.cancelRequested()
                }
            }
        }

        // The read failed, or the copy could not be read: why, and a way back.
        RowLayout {
            objectName: "fileViewerError"
            visible: !viewer.reading && viewer.problem.length > 0
            Layout.fillWidth: true
            Layout.margins: Type.padding
            spacing: Type.padding

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.error
                text: qsTr("! %1").arg(viewer.problem)
            }
            LinkButton {
                objectName: "fileViewerReadAgain"
                visible: viewer.reloadable
                tint: Theme.errorLink
                text: qsTr("Read again")
                onClicked: viewer.askReload()
            }
        }

        // A codec took the file for its own but could not read it: the file is
        // shown as text or hex, and this says why.
        Label {
            objectName: "fileViewerCodecProblem"
            visible: viewer.ready && !!viewer.content.codecError && viewer.content.codecError.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            Layout.bottomMargin: Type.tight
            wrapMode: Text.Wrap
            color: Theme.warning
            text: visible ? qsTr("! %1").arg(viewer.content.codecError) : ""
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: viewer.ready
            currentIndex: viewer.shownMode === "table" ? 0 : viewer.shownMode === "text" ? 1
                        : viewer.shownMode === "decoded" ? 3 : 2

            // Table: the CSV's rows under its headings - sorted, filtered, searched.
            TablePane {
                id: tablePane
                content: viewer.content
            }

            // Text: numbered lines, Find, Go to line, wrap.
            TextPane {
                id: textPane
                content: viewer.content
            }

            // Hex: the painted grid, its bars, the status line and the inspector.
            HexPane {
                id: hexPane
                content: viewer.content
                // A codec's parts of the file, tinted and named in the hex view;
                // bound to the model's own property, which says when they change.
                regions: viewer.content && viewer.content.decoded && viewer.content.decoded.regions
                         ? viewer.content.decoded.regions : []
            }

            // The codec's own view of its model, given as "decoded", and the
            // file as "content" (a view that reads it again uses its `live`).
            Loader {
                id: decodedView
                objectName: "fileViewerDecoded"
                active: viewer.shownMode === "decoded"
                // A new model is a new view: the old one may still hold the
                // model that is about to go.
                onActiveChanged: load()
                Connections {
                    target: viewer.content
                    function onChanged() { decodedView.load() }
                }
                function load() {
                    if (!active || !viewer.content || !viewer.content.decoded) {
                        setSource("")
                        return
                    }
                    if (item && item.decoded === viewer.content.decoded)
                        return
                    setSource(viewer.content.codecView, { decoded: viewer.content.decoded, content: viewer.content })
                }
            }
        }

        // How big, when it was read, what it is, and where it came from.
        Label {
            objectName: "fileViewerFooter"
            visible: viewer.ready
            Layout.fillWidth: true
            Layout.margins: Type.padding
            elide: Text.ElideMiddle
            font.pointSize: Type.caption
            color: Theme.textSecondary
            // The path is the device's text: it goes in last, under the highest number.
            // Once Live has read the device's file, this copy's time is when it
            // was downloaded; the view says when the shown values were read.
            readonly property bool readSince: !!viewer.content && !!viewer.content.live && viewer.content.live.reads > 0
            text: !viewer.ready ? ""
                : (readSince ? qsTr("%1 · downloaded %2 · %3 · %4") : qsTr("%1 · read %2 · %3 · %4"))
                    .arg(viewer.content.sizeText)
                    .arg(Qt.formatTime(viewer.content.readAt, "HH:mm:ss"))
                    .arg(viewer.content.format)
                    .arg(viewer.content.devicePath.length > 0 ? viewer.content.devicePath : viewer.content.localFile)
            // The device's layer, one hover away: the same as the device's
            // bytes, how many differ, or not known - which look alike on screen.
            readonly property string deviceLayer: !viewer.ready || viewer.content.devicePath.length === 0 ? ""
                : !viewer.content.hasReference ? qsTr("The device's bytes are not known for this copy.")
                : viewer.content.referenceDiffers === 0 ? qsTr("The same as %1.").arg(viewer.content.referenceLabel)
                : viewer.content.referenceDiffers === 1 ? qsTr("1 byte differs from %1.").arg(viewer.content.referenceLabel)
                : qsTr("%L1 bytes differ from %2.").arg(viewer.content.referenceDiffers).arg(viewer.content.referenceLabel)
            HoverHandler { id: footerHover }
            ToolTip.visible: footerHover.hovered && (truncated || deviceLayer.length > 0)
            ToolTip.text: deviceLayer.length > 0 ? text + "\n" + deviceLayer : text
        }
    }
}
