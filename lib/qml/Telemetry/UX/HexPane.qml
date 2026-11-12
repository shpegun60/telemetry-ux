// The Viewer's Hex mode: the grid, its scroll bar, Find, Go to, Select block,
// Copy as, the status line, the inspector, and editing. MIT.
//
// The host gives `content`, a FileContent. Keys: Ctrl+F find (Enter / F3 next,
// Shift+Enter / Shift+F3 previous), Ctrl+G go to, Ctrl+E select a block,
// Ctrl+Shift+C copy as, Ctrl+C copy, Ctrl+A select all, Esc collapses the
// selection or closes a bar. Editing is off until Edit: then hex digits (or
// letters in the text column) write over the bytes, Ctrl+V pastes over them,
// Ctrl+H replaces what Find finds, Ctrl+Z and Ctrl+Y undo and redo; the
// viewer's Ctrl+S saves. The status line says how many bytes are changed and
// not saved - Save writes this computer's copy, never the device's file. The
// actions are the viewer's (FileEditTools, on its row or the host's toolbar,
// and its `⋯` menu): they call startEditing(), stopEditing(), save(),
// askDiscard() and saveCopy() here.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Telemetry.UX

FocusScope {
    id: pane
    objectName: "hexPane"

    required property var content
    // A codec's parts of the file ({offset, length, name, path, kind, depth}).
    property var regions: []
    // Which bar is open under the toolbar: "", "find", "goto" or "block".
    property string bar: ""
    // Find's kinds and the widths a number can have, in bytes.
    readonly property var findKinds: [
        { kind: "text", text: qsTr("Text") },
        { kind: "hex", text: qsTr("Hex") },
        { kind: "integer", text: qsTr("Integer") },
        { kind: "float", text: qsTr("Float") }
    ]
    readonly property string findKind: findKinds[findKindBox.currentIndex].kind
    readonly property var findWidths: findKind === "float" ? [4, 8] : [1, 2, 4, 8]
    // The inspector beside the grid when there is room, under it otherwise.
    readonly property bool inspectorBeside: width >= 950
    property bool inspectorOpen: true
    readonly property alias view: grid

    function focusContent() { grid.forceActiveFocus() }
    // Find what is selected: its bytes as hex, so Next and F3 go on with it.
    function findSelection() {
        findKindBox.currentIndex = 1
        findField.text = grid.textAs("hex")
        openBar("find")
        findNext(true)
    }
    function openBar(name) {
        bar = name
        barProblem.text = ""
        Qt.callLater(function() {
            if (name === "find") {
                findField.forceActiveFocus()
                findField.selectAll()
            } else if (name === "goto")
                gotoField.forceActiveFocus()
            else if (name === "block")
                blockStart.forceActiveFocus()
        })
    }
    // Next or previous match of what the Find field holds; the result in words.
    function findNext(forward) {
        if (findField.text.length === 0)
            return
        barProblem.text = grid.find(findField.text, findKind, findWidths[findWidthBox.currentIndex] || 4, forward)
    }
    function closeBar() {
        if (bar === "find")
            grid.clearFind()
        bar = ""
        replacing = false
        barProblem.text = ""
        replaceResult.text = ""
        grid.forceActiveFocus()
    }
    // Replace (Ctrl+H) opens under Find, only while editing.
    property bool replacing: false
    function openReplace() {
        if (!pane.content || grid.size === 0)
            return
        // Without Edit: Find, and why Replace is not there.
        if (!pane.content.editing) {
            openBar("find")
            barProblem.text = qsTr("Turn on Edit to replace.")
            return
        }
        pane.replacing = true
        openBar("find")
        if (findField.text.length > 0)
            Qt.callLater(function() { replaceField.forceActiveFocus(); replaceField.selectAll() })
    }
    function replaceNext(all) {
        barProblem.text = ""
        replaceResult.text = grid.replace(findField.text, findKind, findWidths[findWidthBox.currentIndex] || 4,
                                          replaceField.text, all)
        replaceResult.outcome = grid.outcome
    }
    function paste() { return grid.paste() }
    // Why the last save did not happen; empty after one that did.
    property string saveProblem: ""
    // A copy of a device's file: Save writes this computer's copy, never the device's.
    readonly property bool onDevice: !!content && content.devicePath.length > 0
    function startEditing() {
        pane.content.editing = true
        grid.forceActiveFocus()
    }
    function stopEditing() {
        pane.content.editing = false
        if (pane.visible)
            grid.forceActiveFocus()
    }
    function save() {
        pane.saveProblem = pane.content.save()
        return pane.saveProblem
    }
    function askDiscard() { discardDialog.open() }
    function saveCopy() { copyDialog.open() }
    // A new edit makes an old save problem stale.
    Connections {
        target: pane.content
        function onBytesEdited() { pane.saveProblem = "" }
    }

    Shortcut {
        sequence: "Ctrl+F"
        // Only while the hex view has the keyboard: elsewhere these keys are others'.
        enabled: pane.visible && pane.activeFocus
        onActivated: pane.openBar("find")
    }
    Shortcut {
        sequence: "F3"
        // Only while the hex view has the keyboard: elsewhere these keys are others'.
        enabled: pane.visible && pane.activeFocus && findField.text.length > 0
        onActivated: pane.findNext(true)
    }
    Shortcut {
        sequence: "Shift+F3"
        // Only while the hex view has the keyboard: elsewhere these keys are others'.
        enabled: pane.visible && pane.activeFocus && findField.text.length > 0
        onActivated: pane.findNext(false)
    }
    Shortcut {
        sequence: "Ctrl+H"
        // Only while the hex view has the keyboard; without Edit it says why.
        enabled: pane.visible && pane.activeFocus && !!pane.content && grid.size > 0
        onActivated: pane.openReplace()
    }
    Shortcut {
        sequence: "Ctrl+G"
        // Only while the hex view has the keyboard: elsewhere these keys are others'.
        enabled: pane.visible && pane.activeFocus
        onActivated: pane.openBar("goto")
    }
    Shortcut {
        sequence: "Ctrl+E"
        // Only while the hex view has the keyboard: elsewhere these keys are others'.
        enabled: pane.visible && pane.activeFocus
        onActivated: pane.openBar("block")
    }
    Shortcut {
        sequence: "Ctrl+Shift+C"
        // Only while the hex view has the keyboard: elsewhere these keys are others'.
        enabled: pane.visible && pane.activeFocus && grid.size > 0
        onActivated: copyMenu.popup(grid, 16, grid.headerHeight)
    }

    // Discarding loses work: asked, with how much.
    Dialog {
        id: discardDialog
        objectName: "hexDiscardDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        // A set width: a text that changes with the count would otherwise
        // resize the dialog round its buttons. Over the window, not the pane,
        // which is narrow or not laid out at all while its tab is behind.
        width: Math.max(280, Math.min(440, (parent ? parent.width : 440) - 32))
        modal: true
        title: qsTr("Discard the changes?")
        standardButtons: Dialog.Discard | Dialog.Cancel
        // In a layout, as in UnsavedChangesDialog: a wrapped label straight
        // in a dialog loops its implicit height through the popup's content size.
        ColumnLayout {
            width: parent.width
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: !pane.content || pane.content.editedBytes === 1 ? qsTr("The changed byte goes back to the copy on this computer.")
                      : qsTr("%L1 changed bytes go back to the copy on this computer.").arg(pane.content.editedBytes)
            }
        }
        onDiscarded: {
            pane.content.discard()
            close()
        }
        onClosed: grid.forceActiveFocus()
    }
    FileDialog {
        id: copyDialog
        title: qsTr("Save a copy of the edited bytes")
        fileMode: FileDialog.SaveFile
        onAccepted: pane.saveProblem = pane.content.saveCopyAs(selectedFile)
    }

    // The grid's own menu, on a right click, the Menu key or Shift+F10.
    Menu {
        id: contextMenu
        objectName: "hexContextMenu"
        MenuItem {
            objectName: "hexEditItem"
            text: pane.content && pane.content.editing ? qsTr("Stop &editing") : qsTr("&Edit bytes")
            enabled: grid.size > 0
            onTriggered: pane.content.editing ? pane.content.editing = false : pane.startEditing()
        }
        MenuItem {
            objectName: "hexPasteItem"
            text: qsTr("&Paste over the bytes\tCtrl+V")
            enabled: !!pane.content && pane.content.editing && grid.size > 0
            onTriggered: grid.paste()
        }
        MenuItem {
            objectName: "hexReplaceItem"
            text: qsTr("Repl&ace…\tCtrl+H")
            enabled: !!pane.content && pane.content.editing && grid.size > 0
            onTriggered: pane.openReplace()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("&Copy\tCtrl+C")
            enabled: grid.size > 0
            onTriggered: grid.copy(grid.textPane ? "text" : "hex")
        }
        MenuItem {
            text: qsTr("Copy &as…\tCtrl+Shift+C")
            enabled: grid.size > 0
            onTriggered: copyMenu.popup(grid, contextMenu.x, contextMenu.y)
        }
        MenuItem {
            text: qsTr("Copy &offset")
            enabled: grid.size > 0
            onTriggered: grid.copy("offset")
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Find the &selection")
            enabled: grid.size > 0
            onTriggered: pane.findSelection()
        }
        MenuItem {
            text: qsTr("&Go to…\tCtrl+G")
            onTriggered: pane.openBar("goto")
        }
        MenuItem {
            text: qsTr("Select &block…\tCtrl+E")
            onTriggered: pane.openBar("block")
        }
        MenuItem {
            text: qsTr("Select a&ll\tCtrl+A")
            enabled: grid.size > 0
            onTriggered: grid.selectAll()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Offsets in &decimal")
            checkable: true
            checked: grid.decimalOffsets
            onTriggered: grid.decimalOffsets = checked
        }
        onClosed: grid.forceActiveFocus()
    }

    // Copy as, in the forms people paste.
    Menu {
        id: copyMenu
        objectName: "hexCopyMenu"
        Repeater {
            model: [
                { format: "hex", text: qsTr("Hex (DE AD BE EF)") },
                { format: "hexCompact", text: qsTr("Hex, compact (DEADBEEF)") },
                { format: "text", text: qsTr("Text") },
                { format: "c", text: qsTr("C array") },
                { format: "base64", text: qsTr("Base64") },
                { format: "offset", text: qsTr("Offset") },
                { format: "offsetLength", text: qsTr("Offset and length") }
            ]
            delegate: MenuItem {
                required property var modelData
                text: modelData.text
                onTriggered: grid.copy(modelData.format)
            }
        }
        onClosed: grid.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Go to (Ctrl+G) and Select block (Ctrl+E), in one slot under the toolbar.
        RowLayout {
            objectName: "hexBar"
            visible: pane.bar.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            spacing: 8

            Label {
                text: pane.bar === "find" ? qsTr("Find") : pane.bar === "goto" ? qsTr("Go to offset") : qsTr("Select from")
                color: Theme.textSecondary
            }
            TextField {
                id: findField
                objectName: "hexFindField"
                visible: pane.bar === "find"
                // It gives way first when the Viewer is narrow.
                Layout.fillWidth: true
                Layout.minimumWidth: 96
                Layout.maximumWidth: 260
                font.family: pane.findKind === "text" ? Qt.application.font.family : Type.fixedFamily
                placeholderText: pane.findKind === "hex" ? qsTr("DE AD BE EF")
                               : pane.findKind === "integer" ? qsTr("1000 or 0x3E8")
                               : pane.findKind === "float" ? qsTr("1.25") : qsTr("Text to find")
                Accessible.name: qsTr("Find")
                onAccepted: pane.findNext(true)
                Keys.onPressed: function(event) {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ShiftModifier)) {
                        pane.findNext(false)
                        event.accepted = true
                    }
                }
                Keys.onEscapePressed: pane.closeBar()
            }
            ComboBox {
                id: findKindBox
                objectName: "hexFindKind"
                visible: pane.bar === "find"
                model: pane.findKinds
                textRole: "text"
                Accessible.name: qsTr("Find as")
            }
            ComboBox {
                id: findWidthBox
                objectName: "hexFindWidth"
                visible: pane.bar === "find" && (pane.findKind === "integer" || pane.findKind === "float")
                model: pane.findWidths.map(function(bytes) {
                    return pane.findKind === "float" ? qsTr("%1-bit float").arg(bytes * 8) : qsTr("%1-bit").arg(bytes * 8)
                })
                currentIndex: pane.findKind === "float" ? 0 : 2
                Accessible.name: qsTr("Number size")
            }
            // "3 of 41", "counting…", "10 000+": where the cursor is among the matches.
            Label {
                objectName: "hexFindCount"
                visible: pane.bar === "find" && findField.text.length > 0
                color: Theme.textSecondary
                font.pointSize: Type.caption
                text: grid.counting ? qsTr("counting…")
                    : grid.matchCount === 0 ? ""
                    : grid.matchesCapped ? qsTr("%1 of %2+").arg(grid.matchIndex).arg(grid.matchCount)
                    : qsTr("%1 of %2").arg(grid.matchIndex).arg(grid.matchCount)
            }
            LinkButton {
                objectName: "hexFindPrevious"
                visible: pane.bar === "find"
                tint: Theme.textSecondary
                text: qsTr("Previous")
                onClicked: pane.findNext(false)
            }
            LinkButton {
                objectName: "hexFindNext"
                visible: pane.bar === "find"
                tint: Theme.textSecondary
                text: qsTr("Next")
                onClicked: pane.findNext(true)
            }
            TextField {
                id: gotoField
                objectName: "hexGotoField"
                visible: pane.bar === "goto"
                Layout.preferredWidth: 160
                font.family: Type.fixedFamily
                placeholderText: qsTr("0x1F4, 500, +16, end-16")
                Accessible.name: qsTr("Offset to go to")
                onAccepted: {
                    const problem = grid.goTo(text)
                    barProblem.text = problem
                    if (problem.length === 0)
                        pane.closeBar()
                }
                Keys.onEscapePressed: pane.closeBar()
            }
            TextField {
                id: blockStart
                objectName: "hexBlockStart"
                visible: pane.bar === "block"
                Layout.preferredWidth: 120
                font.family: Type.fixedFamily
                placeholderText: qsTr("start")
                Accessible.name: qsTr("Block start")
                onAccepted: blockOther.forceActiveFocus()
                Keys.onEscapePressed: pane.closeBar()
            }
            ComboBox {
                id: blockMode
                objectName: "hexBlockMode"
                visible: pane.bar === "block"
                model: [qsTr("length"), qsTr("to end offset")]
                Accessible.name: qsTr("Block given by")
            }
            TextField {
                id: blockOther
                objectName: "hexBlockOther"
                visible: pane.bar === "block"
                Layout.preferredWidth: 120
                font.family: Type.fixedFamily
                placeholderText: blockMode.currentIndex === 0 ? qsTr("16") : qsTr("0x203")
                Accessible.name: blockMode.currentIndex === 0 ? qsTr("Block length") : qsTr("Block end")
                onAccepted: {
                    const problem = grid.selectBlock(blockStart.text, text, blockMode.currentIndex === 1)
                    barProblem.text = problem
                    if (problem.length === 0)
                        pane.closeBar()
                }
                Keys.onEscapePressed: pane.closeBar()
            }
            Label {
                id: barProblem
                objectName: "hexBarProblem"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                // "Wrapped to the start" is news, not an error.
                color: text.indexOf(qsTr("Wrapped")) === 0 ? Theme.textSecondary : Theme.error
                elide: Text.ElideRight
                Accessible.role: Accessible.AlertMessage
            }
            ToolButton {
                objectName: "hexBarClose"
                flat: true
                implicitWidth: 24
                implicitHeight: 24
                text: qsTr("×")
                Accessible.name: qsTr("Close the bar")
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: qsTr("Close (Esc)")
                onClicked: pane.closeBar()
            }
        }

        // Replace (Ctrl+H, while editing), under Find: the same kind of value,
        // the same length, so the file keeps its size.
        RowLayout {
            objectName: "hexReplaceBar"
            visible: pane.bar === "find" && pane.replacing && !!pane.content && pane.content.editing
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            spacing: 8
            Label {
                text: qsTr("Replace with")
                color: Theme.textSecondary
            }
            TextField {
                id: replaceField
                objectName: "hexReplaceField"
                Layout.fillWidth: true
                Layout.minimumWidth: 96
                Layout.maximumWidth: 260
                font.family: pane.findKind === "text" ? Qt.application.font.family : Type.fixedFamily
                placeholderText: qsTr("The same length as what is found")
                Accessible.name: qsTr("Replace with")
                onAccepted: pane.replaceNext(false)
                Keys.onEscapePressed: pane.closeBar()
            }
            // The replacement's length beside it, in the error colour when it
            // is not what Find finds: seen before pressing.
            Label {
                objectName: "hexReplaceSize"
                readonly property int width_: pane.findWidths[findWidthBox.currentIndex] || 4
                readonly property int replaceBytes: replaceField.text.length > 0 ? grid.patternBytes(replaceField.text, pane.findKind, width_) : -1
                readonly property int findBytes: findField.text.length > 0 ? grid.patternBytes(findField.text, pane.findKind, width_) : -1
                visible: replaceBytes >= 0
                font.pointSize: Type.caption
                color: findBytes >= 0 && replaceBytes !== findBytes ? Theme.error : Theme.textSecondary
                text: findBytes >= 0 && replaceBytes !== findBytes
                      ? qsTr("%L1 bytes, not %L2").arg(replaceBytes).arg(findBytes)
                      : replaceBytes === 1 ? qsTr("1 byte") : qsTr("%L1 bytes").arg(replaceBytes)
            }
            LinkButton {
                objectName: "hexReplaceOne"
                tint: Theme.textSecondary
                text: qsTr("Replace")
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: qsTr("Write over the match selected and select the next (Enter)")
                onClicked: pane.replaceNext(false)
            }
            LinkButton {
                objectName: "hexReplaceAll"
                tint: Theme.textSecondary
                text: qsTr("Replace all")
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: qsTr("Write over every match; one Undo takes it all back")
                onClicked: pane.replaceNext(true)
            }
            Item { Layout.fillWidth: true }
        }
        // What Replace did ("Replaced 12"), or why not, whole: a line of its
        // own under the Replace row, wrapped, never cut. Its colour from the
        // outcome the grid gives, never from the words.
        Label {
            id: replaceResult
            objectName: "hexReplaceResult"
            property string outcome: ""
            visible: pane.bar === "find" && pane.replacing && text.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            wrapMode: Text.Wrap
            color: outcome === "done" ? Theme.textSecondary : outcome === "partial" ? Theme.warning : Theme.error
            Accessible.role: Accessible.AlertMessage
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                HexView {
                    id: grid
                    objectName: "fileViewerHex"
                    anchors.fill: parent
                    anchors.rightMargin: scroll.width
                    content: pane.content
                    regions: pane.regions
                    font.family: Type.fixedFamily
                    font.pointSize: Type.body
                    // From the font, so a smaller density still fits its text.
                    rowHeight: Math.ceil(hexMetrics.height) + 4
                    textColor: Theme.textPrimary
                    secondaryColor: Theme.textSecondary
                    mutedColor: Theme.textMuted
                    selectionColor: Theme.selectionFill
                    selectedTextColor: Theme.selectedForeground
                    focusColor: Theme.focusRing
                    regionColor: Theme.regionTint
                    outlineColor: Theme.outline
                    matchColor: Theme.infoContainer
                    matchOutlineColor: Theme.info
                    editedColor: Theme.warning
                    deviceColor: Theme.info
                    Accessible.role: Accessible.Table
                    Accessible.name: grid.accessibleText
                    Accessible.description: qsTr("Arrows move by byte. Tab switches to the text column.")
                    onContextMenuRequested: function(x, y) { contextMenu.popup(grid, x, y) }
                    ToolTip.visible: grid.offsetHeadingHovered
                    ToolTip.text: grid.decimalOffsets ? qsTr("Show offsets in hex") : qsTr("Show offsets in decimal")
                    ToolTip.delay: 500
                }
                FontMetrics {
                    id: hexMetrics
                    font: grid.font
                }
                ScrollBar {
                    id: scroll
                    objectName: "hexScroll"
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.topMargin: grid.headerHeight
                    orientation: Qt.Vertical
                    policy: grid.rowCount > grid.visibleRows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    size: grid.rowCount > 0 ? Math.min(1, grid.visibleRows / grid.rowCount) : 1
                    position: grid.rowCount > 0 ? grid.topRow / grid.rowCount : 0
                    onPositionChanged: if (pressed) grid.topRow = Math.round(position * grid.rowCount)
                }
            }

            // The inspector beside the grid, from 950 px.
            InspectorTable {
                visible: pane.inspectorOpen && pane.inspectorBeside
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                view: grid
            }
        }

        // The inspector under the grid when narrower: one line, the common
        // types, led by the byte order it reads them in, which a click switches.
        RowLayout {
            visible: pane.inspectorOpen && !pane.inspectorBeside && grid.size > 0
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 6
            LinkButton {
                objectName: "hexByteOrder"
                tint: Theme.textSecondary
                text: grid.bigEndian ? qsTr("BE") : qsTr("LE")
                Accessible.name: grid.bigEndian ? qsTr("Byte order: big-endian") : qsTr("Byte order: little-endian")
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: grid.bigEndian ? qsTr("Big-endian; click for little-endian")
                                             : qsTr("Little-endian; click for big-endian")
                onClicked: grid.bigEndian = !grid.bigEndian
            }
        Label {
            objectName: "hexInspectorLine"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            elide: Text.ElideRight
            font.family: Type.fixedFamily
            font.pointSize: Type.caption
            color: Theme.textSecondary
            text: {
                const rows = grid.inspector
                const pick = ["int16", "uint32", "float32"]
                const parts = []
                for (let i = 0; i < rows.length; ++i)
                    if (pick.indexOf(rows[i].type) >= 0)
                        parts.push(rows[i].type + " " + rows[i].value)
                return parts.join(" · ")
            }
        }
        }

        // The status line: where the cursor is, what is selected, a notice.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 12
            // Editing, or edits not saved: said first, in the warning colour; a
            // save that failed says why here.
            Label {
                objectName: "hexEditState"
                // Saved here but not the device's as read: the third layer.
                readonly property bool deviceDiffers: !!pane.content && pane.content.hasReference
                                                      && pane.content.referenceDiffers > 0
                readonly property bool editState: !!pane.content && (pane.content.editing || pane.content.modified)
                // Just written to the device and read back the same: said once,
                // where the difference would be.
                readonly property bool writtenSame: !!pane.content && pane.content.hasReference
                                                    && pane.content.referenceWritten && pane.content.referenceDiffers === 0
                visible: editState || deviceDiffers || writtenSame || pane.saveProblem.length > 0
                Layout.maximumWidth: pane.width / 2
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: pane.saveProblem.length > 0 ? Theme.error : editState ? Theme.warning : Theme.info
                text: {
                    if (pane.saveProblem.length > 0)
                        return pane.saveProblem
                    if (!pane.content)
                        return ""
                    if (!editState && !deviceDiffers)
                        return qsTr("Written to the device · checked")
                    if (!editState)
                        // Short, so it fits beside the cursor's place; when the
                        // device's bytes were read is in the tooltip.
                        return pane.content.referenceDiffers === 1
                               ? qsTr("Saved here · 1 byte differs from the device")
                               : qsTr("Saved here · %L1 bytes differ from the device").arg(pane.content.referenceDiffers)
                    const edited = pane.content.editedBytes
                    const state = pane.content.editing ? qsTr("Editing") : qsTr("Not saved")
                    return edited === 0 ? state
                         : edited === 1 ? qsTr("%1 · 1 byte changed").arg(state)
                         : qsTr("%1 · %L2 bytes changed").arg(state).arg(edited)
                }
                HoverHandler { id: editStateHover }
                ToolTip.visible: editStateHover.hovered && !!pane.content
                                 && (pane.content.editedBytes > 0 || deviceDiffers || writtenSame)
                ToolTip.text: !editState && !deviceDiffers
                              ? qsTr("Same as %1").arg(pane.content ? pane.content.referenceLabel : "")
                            : !editState ? qsTr("This copy is saved on this computer and differs from %1; Restore the device's version puts them back")
                                                .arg(pane.content ? pane.content.referenceLabel : "")
                            : pane.onDevice ? qsTr("Changed in the copy on this computer, not saved; the device's file is unchanged")
                                            : qsTr("Changed, not saved")
                Accessible.role: Accessible.StaticText
                Accessible.description: ToolTip.text
            }
            Label {
                objectName: "hexStatus"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                font.pointSize: Type.caption
                // A paste that went in whole is news; cut short a warning; refused an error.
                color: grid.notice.length === 0 || grid.outcome === "done" ? Theme.textSecondary
                     : grid.outcome === "refused" ? Theme.error : Theme.warning
                text: grid.notice.length > 0 ? grid.notice : grid.status
                // Cut by a narrow Viewer: the whole of it on hover.
                HoverHandler { id: statusHover }
                ToolTip.visible: statusHover.hovered && truncated
                ToolTip.text: text
            }
            LinkButton {
                objectName: "hexInspectorToggle"
                tint: Theme.textSecondary
                text: pane.inspectorOpen ? qsTr("Hide inspector") : qsTr("Inspector")
                onClicked: pane.inspectorOpen = !pane.inspectorOpen
            }
        }
    }
}
