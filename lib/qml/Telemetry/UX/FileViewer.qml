// One file in the viewer: its format's own view when a codec read it, a table
// when it is CSV, text when it is text, hex always, with a switch between the
// ones it has. Hex edits the bytes once Edit is on; Read again from the
// device asks first when edits are not saved. MIT.
//
// Editing's actions - Edit, Save, Undo, Redo, as pictograms (FileEditTools) -
// are on the viewer's top row; a host with a toolbar of its own sets
// editToolsShown to false and puts a FileEditTools there for the viewer in
// front, as an editor's toolbar acts on the editor in front. Either calls
// startEditing(), stopEditing(), save(); askDiscard() and saveCopy() are
// also in the `⋯` menu. A host that offers the file's actions elsewhere -
// a tab's right-click menu - sets menuButtonShown to false; the top row then
// appears only for a file with more than one view, to switch between them.
//
// The host gives `content`, a FileContent - usually a row of OpenFiles, whose
// role of that name fills it in a view's delegate - and answers the requests:
// reloadRequested() to read the device's file again, cancelRequested() while
// it is being read, openExternallyRequested and showInFolderRequested with
// the local file, closeRequested. The file's actions are in its `⋯` menu (and
// the host's tab menu), not in a row of buttons. Save a copy as… writes what
// the tab shows itself (saveCopy()); saveAsRequested is no longer sent.
//
// The footer says first how the copy stands with the device's file (FL-1e):
// the same, saved differently in the project, written and read back, changed
// on the device since it was read, not known, read-only - "· not connected"
// while its device is not (deviceConnected) - then its size, format and
// path; the project's path and the times on hover. A change of it is
// announced to a screen reader. A descriptor whose fingerprint is not the
// connected device's says so over its view (FL-1f).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

Pane {
    id: viewer
    objectName: "fileViewer"
    padding: 0

    required property var content
    property bool retiring: false
    // The owner calls this before releasing the viewer. Its decoded child is
    // prepared while it still has a live context, before Loader clears it.
    function prepareForUnload() {
        if (retiring)
            return
        retiring = true
        if (decodedView) {
            decodedView.prepareItem()
            decodedView.setSource("")
        }
        if (moreMenu)
            moreMenu.close()
    }
    // No external callbacks during context teardown. The owner calls prepare
    // explicitly, or OpenFiles announces logical retirement before removal.
    Component.onDestruction: retiring = true
    // The host can read the device's file again (connected, the file listed).
    property bool reloadable: false
    // This copy's device is the one connected: the state line says
    // "· not connected" otherwise. As reloadable unless the host says.
    property bool deviceConnected: reloadable
    // After "Read-only · " in the state line, why in a few words ("made by
    // the device's firmware"); the host's, as its read-only reason is.
    property string readOnlyNote: ""
    // "decoded", "table", "text" or "hex"; empty for the format's own first choice.
    property string mode: ""
    // Editing's pictograms on the top row; false when the host has them on its toolbar.
    property bool editToolsShown: true
    // The `⋯` button on the top row; false when the host has one of its own.
    property bool menuButtonShown: true
    readonly property bool ready: !!content && content.loaded
    readonly property bool reading: !!content && content.loading
    readonly property string problem: content ? content.error : ""
    // A codec took the file for its own and could not read it (codecError says why).
    readonly property bool codecFailed: ready && !!content.codecError && content.codecError.length > 0
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
    // Kept for hosts that handled it; the menu now writes the copy itself.
    signal saveAsRequested(string localFile)
    signal closeRequested()

    // The device's path, or the local file's for one from disk.
    readonly property string shownPath: !content ? ""
        : content.devicePath.length > 0 ? content.devicePath : content.localFile
    function openMenu() { moreMenu.popup(moreButton, 0, moreButton.height) }
    // Editing, from the row, a toolbar or a menu. Edit shows the bytes first.
    function startEditing() {
        if (!viewer.ready || viewer.content.size === 0 || viewer.content.readOnlyReason.length > 0)
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
    // Save a copy as…: what the tab shows, edits not saved included.
    function saveCopy() {
        if (viewer.ready)
            hexPane.saveCopy()
    }
    function saveEditedCopy() { saveCopy() }
    // "12:04", with the date when not today.
    function timeText(when) {
        if (!when || !(when instanceof Date) || isNaN(when.getTime()))
            return ""
        return when.toDateString() === new Date().toDateString()
               ? when.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
               : when.toLocaleString(Qt.locale(), Locale.ShortFormat)
    }
    // A fingerprint's first four digits, as the designer writes them: "3f2a".
    function fingerprintStart(fingerprint) {
        return String(fingerprint).substring(2, 6).toLowerCase()
    }
    // FL-1f: a descriptor that reads, from a device, whose fingerprint is not
    // the connected device's - the firmware changed, or another device's.
    readonly property bool otherDescriptor: ready && !!content.decoded && content.codecName === "Descriptor"
        && content.devicePath.length > 0 && content.fingerprint.length > 0
        && content.connectedFingerprint.length > 0 && content.fingerprint !== content.connectedFingerprint
    // The device's file changed after its bytes here were read: a write found
    // it so, or the connected device gave another descriptor.
    readonly property bool deviceFileChanged: ready && content.devicePath.length > 0 && content.hasReference
        && (content.deviceChanged || (viewer.deviceConnected && content.referenceFingerprint.length > 0
            && content.connectedFingerprint.length > 0
            && content.referenceFingerprint !== content.connectedFingerprint))
    // How the copy stands with the device's file: "same", "saved",
    // "written", "writtenDiffers", "changed", "unknown", "readOnly"; empty
    // for a file from disk.
    readonly property string stateKind: {
        if (!ready || content.devicePath.length === 0)
            return ""
        if (!content.hasReference)
            return "unknown"
        if (deviceFileChanged)
            return "changed"
        // Written and read back, and not saved since.
        if (content.referenceWritten && !(content.savedAt > content.referenceAt))
            return content.savedDiffers > 0 ? "writtenDiffers" : "written"
        if (content.savedDiffers > 0)
            return "saved"
        if (content.readOnlyReason.length > 0)
            return "readOnly"
        return "same"
    }
    // The designer's words for it.
    readonly property string stateText: {
        if (stateKind.length === 0)
            return ""
        const when = timeText(content.referenceAt)
        const count = content.savedDiffers
        let line = ""
        switch (stateKind) {
        case "unknown": line = qsTr("The device's bytes are not known"); break
        case "changed": line = qsTr("! The device's file changed after %1").arg(when); break
        case "written": line = qsTr("Written %1 · read back the same").arg(when); break
        case "writtenDiffers":
            line = count === 1 ? qsTr("Written %1 · 1 byte reads back different").arg(when)
                               : qsTr("Written %1 · %L2 bytes read back different").arg(when).arg(count)
            break
        case "saved":
            line = count === 1 ? qsTr("Saved in the project · 1 byte differs from the device's")
                               : qsTr("Saved in the project · %L1 bytes differ from the device's").arg(count)
            break
        case "readOnly":
            line = viewer.readOnlyNote.length > 0 ? qsTr("Read-only · %1").arg(viewer.readOnlyNote) : qsTr("Read-only")
            break
        default: line = qsTr("Same as the device's · read %1").arg(when)
        }
        return viewer.deviceConnected ? line : qsTr("%1 · not connected").arg(line)
    }
    // A change of the state line is news; the first one of a file is not.
    property var announcedContent: null
    property string announcedState: ""
    onStateTextChanged: {
        if (!viewer.content || viewer.retiring)
            return
        if (viewer.announcedContent !== viewer.content) {
            viewer.announcedContent = viewer.content
            viewer.announcedState = viewer.stateText
            return
        }
        if (viewer.stateText.length > 0 && viewer.stateText !== viewer.announcedState && viewer.visible)
            viewer.Accessible.announce(viewer.stateText)
        viewer.announcedState = viewer.stateText
    }
    // Read again from the device loses edits not saved: asked first, then read again.
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
                // A device's file only; a file from disk has nothing to read again.
                visible: !!viewer.content && viewer.content.devicePath.length > 0
                height: visible ? implicitHeight : 0
                text: qsTr("&Read again from the device%1\tCtrl+R").arg(viewer.reloadable ? "" : qsTr(" (not connected)"))
                enabled: viewer.reloadable && !viewer.reading
                onTriggered: viewer.askReload()
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
                // What the tab shows, edits not saved included.
                text: qsTr("Save a cop&y as…\tCtrl+Shift+S")
                enabled: viewer.ready
                onTriggered: viewer.saveCopy()
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
        // shown as text or hex, and this says why - then the ways back to a
        // copy that reads: the device's bytes as read, or the device's file now.
        ColumnLayout {
            objectName: "fileViewerCodecProblemBar"
            visible: viewer.codecFailed
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            Layout.bottomMargin: Type.tight
            spacing: Type.tight
            Label {
                id: codecProblem
                objectName: "fileViewerCodecProblem"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.warning
                text: viewer.codecFailed ? viewer.content.codecError : ""
            }
            Flow {
                visible: viewer.restorable || viewer.reloadable
                Layout.fillWidth: true
                spacing: Type.groupSpacing
                LinkButton {
                    id: restoreLink
                    objectName: "fileViewerCodecRestore"
                    visible: viewer.restorable
                    text: qsTr("Restore the device's version")
                    onClicked: viewer.askRestore()
                }
                LinkButton {
                    id: readAgainLink
                    objectName: "fileViewerCodecReadAgain"
                    visible: viewer.reloadable
                    text: qsTr("Read again from the device")
                    onClicked: viewer.askReload()
                }
            }
        }

        // FL-1f: a descriptor that is not the connected device's - its
        // firmware changed since this copy was read, or it is another
        // device's - says so over its view, with the way to the device's own.
        ColumnLayout {
            objectName: "fileViewerOtherDescriptorBar"
            visible: viewer.otherDescriptor
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            Layout.bottomMargin: Type.tight
            spacing: Type.tight
            Label {
                objectName: "fileViewerOtherDescriptor"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.warning
                text: viewer.otherDescriptor
                      ? qsTr("This descriptor is not the connected device's (%1… against %2…)")
                            .arg(viewer.fingerprintStart(viewer.content.fingerprint))
                            .arg(viewer.fingerprintStart(viewer.content.connectedFingerprint))
                      : ""
                Accessible.role: Accessible.AlertMessage
                HoverHandler { id: otherHover }
                ToolTip.visible: otherHover.hovered && viewer.otherDescriptor
                ToolTip.text: viewer.otherDescriptor
                              ? qsTr("This file's fingerprint is %1; the connected device's is %2.")
                                    .arg(viewer.content.fingerprint).arg(viewer.content.connectedFingerprint)
                              : ""
            }
            LinkButton {
                objectName: "fileViewerOtherDescriptorReadAgain"
                visible: viewer.reloadable
                text: qsTr("Read again from the device")
                onClicked: viewer.askReload()
            }
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
                // A copy that could not be saved says why in Hex.
                onCopyProblem: viewer.mode = "hex"
                inspectorActive: !viewer.retiring
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
                // Keep active at its default true. setActive(false) clears a
                // loaded item's context BEFORE activeChanged; cleanup there
                // would already be too late. Source lifetime is owned below.
                property bool replacing: false
                Component.onCompleted: load()
                Connections {
                    target: viewer
                    function onShownModeChanged() { decodedView.load() }
                    function onContentChanged() {
                        if (viewer.content && viewer.content.retiring !== true)
                            viewer.retiring = false
                        decodedView.load()
                    }
                }
                Connections {
                    target: viewer.content
                    ignoreUnknownSignals: true
                    function onChanged() { decodedView.load() }
                    function onRetiringChanged() {
                        if (viewer.content && viewer.content.retiring === true)
                            viewer.prepareForUnload()
                    }
                }
                function prepareItem() {
                    if (item && typeof item.prepareForUnload === "function")
                        item.prepareForUnload()
                }
                function load() {
                    if (replacing || viewer.retiring)
                        return
                    const wanted = viewer.shownMode === "decoded" && !!viewer.content && !!viewer.content.decoded
                    if (wanted && item && item.decoded === viewer.content.decoded && item.content === viewer.content)
                        return
                    replacing = true
                    try {
                        prepareItem()
                        // prepare may notify the owner; re-read the current
                        // target rather than carrying a borrowed model across it.
                        if (viewer.retiring || viewer.shownMode !== "decoded" || !viewer.content || !viewer.content.decoded)
                            setSource("")
                        else
                            setSource(viewer.content.codecView, { decoded: viewer.content.decoded, content: viewer.content })
                    } finally {
                        replacing = false
                    }
                }
            }
        }

        // How the copy stands with the device's file (the state line), then
        // how big, what it is, and where it came from.
        RowLayout {
            id: footer
            objectName: "fileViewerFooter"
            visible: viewer.ready
            Layout.fillWidth: true
            Layout.margins: Type.padding
            spacing: 0
            // The path is the device's text: it goes in last, under the highest number.
            readonly property string facts: !viewer.ready ? ""
                : qsTr("%1 · %2 · %3").arg(viewer.content.sizeText).arg(viewer.content.format)
                    .arg(viewer.content.devicePath.length > 0 ? viewer.content.devicePath : viewer.content.localFile)
            // The whole line, as a screen reader and a check read it.
            readonly property string text: viewer.stateText.length > 0
                ? qsTr("%1 · %2").arg(viewer.stateText).arg(facts) : facts
            // On hover: where the copy is in the project, and the times.
            readonly property string details: {
                if (!viewer.ready)
                    return ""
                const lines = [qsTr("In the project: %1").arg(viewer.content.localFile)]
                if (viewer.content.devicePath.length > 0)
                    lines.push(qsTr("On the device: %1").arg(viewer.content.devicePath))
                const read = viewer.timeText(viewer.content.referenceAt)
                if (viewer.content.hasReference && read.length > 0)
                    lines.push(viewer.content.referenceWritten ? qsTr("Written to the device and read back: %1").arg(read)
                                                               : qsTr("Read from the device: %1").arg(read))
                const saved = viewer.timeText(viewer.content.savedAt)
                if (saved.length > 0)
                    lines.push(qsTr("Saved in the project: %1").arg(saved))
                return lines.join("\n")
            }
            Label {
                objectName: "fileViewerState"
                visible: viewer.stateText.length > 0
                // Most of the line at most: the path keeps the rest.
                Layout.maximumWidth: Math.max(0, viewer.width * 0.6)
                elide: Text.ElideRight
                font.pointSize: Type.caption
                // A change on the device or a read-back that differs is a
                // warning; bytes saved differently the device's layer's info
                // colour; not known stays quiet.
                color: viewer.stateKind === "changed" || viewer.stateKind === "writtenDiffers" ? Theme.warning
                     : viewer.stateKind === "saved" ? Theme.info
                     : viewer.stateKind === "unknown" ? Theme.textMuted : Theme.textSecondary
                text: qsTr("%1 · ").arg(viewer.stateText)
            }
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideMiddle
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: footer.facts
            }
            HoverHandler { id: footerHover }
            ToolTip.visible: footerHover.hovered && footer.details.length > 0
            ToolTip.text: footer.details
            Accessible.role: Accessible.StaticText
            Accessible.name: footer.text
            Accessible.description: footer.details
        }
    }
}
