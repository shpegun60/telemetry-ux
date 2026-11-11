// The Viewer's Hex mode: the grid, its scroll bar, Find, Go to, Select block,
// Copy as, the status line and the inspector. MIT.
//
// The host gives `content`, a FileContent. Keys: Ctrl+F find (Enter / F3 next,
// Shift+Enter / Shift+F3 previous), Ctrl+G go to, Ctrl+E select a block,
// Ctrl+Shift+C copy as, Ctrl+C copy, Ctrl+A select all, Esc collapses the
// selection or closes a bar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
        barProblem.text = ""
        grid.forceActiveFocus()
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

    // The grid's own menu, on a right click, the Menu key or Shift+F10.
    Menu {
        id: contextMenu
        objectName: "hexContextMenu"
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
            Label {
                objectName: "hexStatus"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: grid.notice.length > 0 ? Theme.warning : Theme.textSecondary
                text: grid.notice.length > 0 ? grid.notice : grid.status
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
