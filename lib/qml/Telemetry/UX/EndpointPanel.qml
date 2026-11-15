// Field/Command/Service Details (family spec C): a header with the path and the
// panel's one accent action, line 2 in words, one status line, and quiet
// striped tables whose value cells are the editor. The host owns the provider,
// buffers and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQml.Models

Pane {
    id: panel
    palette.window: Theme.surfaceWindow
    palette.windowText: Theme.textPrimary
    palette.base: Theme.surface
    palette.alternateBase: Theme.surfaceAlternate
    palette.text: Theme.textPrimary
    palette.button: Theme.buttonFill
    palette.buttonText: Theme.textPrimary
    palette.highlight: Theme.stateSelected
    palette.highlightedText: Theme.selectedForeground
    palette.placeholderText: Theme.textMuted
    // Kept for hosts that set it: the panel has no card any more, only its
    // padding follows.
    property bool framed: true
    padding: framed ? Type.groupSpacing : Type.padding
    // What Details sends is never undone (UR-1). A host with an UndoRouter
    // gives these words to the area that holds the panel -
    // `UndoScope.refusal: details.undoRefusal` - so Ctrl+Z there outside a
    // field takes back nothing and says why; the Edit menu still names its step.
    readonly property string undoRefusal: provider.commandSelected || provider.serviceSelected
                                          ? qsTr("Undo does not take back what is sent to the device.")
                                          : qsTr("Undo does not take back a write. To go back, apply the old value.")
    readonly property bool hasSelection: provider.selection.length > 0
    required property var provider
    property int categoryFilter: 0
    property Component headerComponent: null
    property Component footerComponent: null
    // The header's title: the path. A host that shows something else (the
    // Batch's pane) names it otherwise.
    property string title: provider.selection
    // Words before line 2, as the Batch's pane says "Item 3 · ".
    property string infoPrefix: ""
    // Replaces the header's own actions (Read, Cancel, Apply, Execute, Call).
    property Component actionsComponent: null
    // What the response area says before the first call, and the panel with
    // nothing selected.
    property string emptyResponseText: qsTr("Call to see the response.")
    property string emptyText: qsTr("Select a Field, Command or Service in the Device tree.")
    readonly property int selectedCategory: provider.commandSelected ? 2 : provider.serviceSelected ? 3 : 1
    readonly property bool selectionMatches: categoryFilter === 0 || (provider.selection.length > 0 && selectedCategory === categoryFilter)
    readonly property bool disconnected: typeof provider.connected === "boolean" && !provider.connected
    readonly property var selectionInfo: typeof provider.selectionInfo === "object" && provider.selectionInfo ? provider.selectionInfo : ({})
    property int valueModelRevision: 0
    property int responseModelRevision: 0
    // Changes are off: what the selection would need them for, in words.
    readonly property bool changesOff: typeof provider.allowMutations === "boolean" && !provider.allowMutations
    property string refusalText: !showSelection || !hasSelection || disconnected || !changesOff ? ""
        : provider.commandSelected ? qsTr("Changes are off. Allow changes to execute this Command.")
        : provider.serviceSelected ? qsTr("Changes are off. Allow changes to call this Service.")
        : selectionInfo.writable === true ? qsTr("Changes are off. Allow changes to edit this Field.") : ""
    // A Command or Service that takes nothing: a caption, not an empty table.
    readonly property bool noArguments: {
        void valueModelRevision
        return showSelection && hasSelection && (provider.commandSelected || provider.serviceSelected) && !provider.busy
            && !!provider.values && typeof provider.values.rowCount === "function" && provider.values.rowCount() === 0
    }
    readonly property bool showMetadata: {
        void valueModelRevision
        if (!showSelection || disconnected || !hasSelection || selectedCategory !== 1 || provider.editing ||
            typeof provider.definitions !== "object" || !provider.definitions ||
            typeof provider.definitionRootIndex === "undefined")
            return false
        return provider.values && typeof provider.values.rowCount === "function" && provider.values.rowCount() === 0
    }
    // The complete Configurator keeps its empty workspace and host extension
    // slots while offline. Category-only panels show retained outcomes alone.
    readonly property bool showSelection: selectionMatches && (categoryFilter === 0 || !disconnected)
    readonly property bool showResponse: (categoryFilter === 0 || categoryFilter === 3) && provider.serviceSelected &&
        !disconnected && (provider.serviceResponseEndpoint.length === 0 || provider.serviceResponseEndpoint === provider.selection)
    // Request and Response stand side by side from this width.
    readonly property real minimumTableWidth: Type.bodyPixels * 27
    property string contextHelp: provider.serviceSelected
        ? qsTr("Request values start at zero, not at device defaults. Call sends one request. The response is kept until the next accepted change or session.")
        : provider.commandSelected
        ? qsTr("Arguments start at zero, not at device defaults. Execute sends the Command once. Accepted does not confirm that the device completed it.")
        : provider.editing
        ? qsTr("You are editing a draft. Apply writes it once, then reads the device again. Floating-point values show the exact encoded value below the input.")
        : qsTr("The type and structure come from the Descriptor. Read reads the Field; expanding its definition does not send requests.")

    // Line 2: the selection in words, "Field · f32 · 4 bytes · read only".
    readonly property string selectionLine: {
        if (typeof selectionInfo.detail === "string" && selectionInfo.detail.length > 0)
            return infoPrefix + selectionInfo.detail
        const parts = [selectedCategory === 2 ? qsTr("Command") : selectedCategory === 3 ? qsTr("Service") : qsTr("Field")]
        if (typeof selectionInfo.typeText === "string" && selectionInfo.typeText.length > 0)
            parts.push(selectionInfo.typeText)
        if (typeof selectionInfo.wireBytes === "number" && selectionInfo.wireBytes > 0)
            parts.push(selectionInfo.wireBytes === 1 ? qsTr("1 byte") : qsTr("%L1 bytes").arg(selectionInfo.wireBytes))
        if (selectedCategory === 1 && typeof selectionInfo.readable === "boolean" && typeof selectionInfo.writable === "boolean")
            parts.push(selectionInfo.readable && selectionInfo.writable ? qsTr("read and write")
                : selectionInfo.readable ? qsTr("read only") : selectionInfo.writable ? qsTr("write only") : qsTr("no access"))
        return infoPrefix + parts.join(" · ")
    }
    // The status line: {text, failed, tip}. A provider that says it
    // (detailsStatus) is taken at its word; otherwise it is made here.
    readonly property var statusInfo: {
        void valueModelRevision
        if (typeof provider.detailsStatus === "object" && provider.detailsStatus
            && typeof provider.detailsStatus.text === "string")
            return provider.detailsStatus
        if (!hasSelection)
            return ({ text: "", failed: false, tip: "" })
        if (provider.busy)
            return ({ text: provider.commandSelected ? qsTr("Executing…") : provider.serviceSelected ? qsTr("Calling…")
                            : qsTr("Working…"), failed: false, tip: "" })
        if (selectedCategory === 1 && provider.editing)
            return ({ text: qsTr("Edited, not applied"), failed: false, tip: "" })
        const outcome = selectedCategory === 2 ? provider.commandOutcome : selectedCategory === 3 ? provider.serviceOutcome
            : provider.writeOutcome
        const attention = selectedCategory === 2 ? provider.commandOutcomeNeedsAttention
            : selectedCategory === 3 ? provider.serviceOutcomeNeedsAttention : provider.writeOutcomeNeedsAttention
        if (typeof outcome === "string" && outcome.length > 0)
            return ({ text: outcome, failed: attention === true, tip: "" })
        const read = !!provider.values && typeof provider.values.rowCount === "function" && provider.values.rowCount() > 0
        return ({ text: selectedCategory === 2 ? qsTr("Not executed yet") : selectedCategory === 3 ? qsTr("Not called yet")
                        : read ? qsTr("Read") : qsTr("Not read yet"), failed: false, tip: "" })
    }
    // What a category panel keeps from before the connection dropped.
    readonly property string retainedText: {
        if (!disconnected)
            return ""
        const parts = []
        // A category panel keeps its kind's; the whole panel the selection's,
        // or every kind's with nothing selected.
        const kind = categoryFilter !== 0 ? categoryFilter : hasSelection ? selectedCategory : 0
        const add = function(category, text) {
            if ((kind === 0 || kind === category) && typeof text === "string" && text.length > 0)
                parts.push(text)
        }
        add(1, provider.writeOutcome)
        add(2, provider.commandOutcome)
        add(3, provider.serviceOutcome)
        return parts.length > 0 ? qsTr("Before the connection dropped: %1").arg(parts.join(" · ")) : ""
    }
    background: Rectangle { color: Theme.surfaceWindow }

    // Columns (family spec A3): Name fills; Value is as wide as the widest
    // value shown, growing only; Type as wide as the widest type, 40 to 128 px;
    // Type hides before Name gets narrower than 96 px. Nothing scrolls sideways.
    property real valueSeen: 0
    property real numberSeen: 0
    property real typeSeen: 0
    function noteValue(width, numeric) {
        if (width > valueSeen) valueSeen = width
        if (numeric && width > numberSeen) numberSeen = width
    }
    function noteType(width) { if (width > typeSeen) typeSeen = width }
    function resetColumns() { valueSeen = 0; numberSeen = 0; typeSeen = 0 }
    Connections { target: Type; function onBodyChanged() { panel.resetColumns() } }
    function valueWidth(width) {
        const most = Math.max(Math.min(Type.bodyPixels * 16.5, width * 0.4), numberSeen)
        return Math.min(Math.max(Type.bodyPixels * 4.2, valueSeen), most)
    }
    function typeWidth() { return Math.min(Type.bodyPixels * 9.6, Math.max(Type.bodyPixels * 3, typeSeen)) }
    function typeShown(width) {
        return width - Type.padding * 2 - valueWidth(width) - typeWidth() - Type.columnGap * 2 - Type.indent
            >= Type.bodyPixels * 7.2
    }
    function typeX(width) { return width - Type.padding - typeWidth() }
    function valueX(width) { return (typeShown(width) ? typeX(width) - Type.columnGap : width - Type.padding) - valueWidth(width) }
    function nameEnd(width) { return valueX(width) - Type.columnGap }

    // The panel's one primary action: Apply, Execute or Call.
    component AccentButton: Button {
        font.weight: Type.headingWeight
        palette.button: Theme.accent
        palette.buttonText: Theme.accentForeground
        palette.brightText: Theme.accentForeground
        palette.disabled.button: Theme.surfaceDisabled
        palette.disabled.buttonText: Theme.textDisabled
    }
    // A failure's 12 px glyph: a ring with "!". Pure QML, so a host needs none
    // of the module's C++ types for it.
    component ErrorGlyph: Shape {
        id: glyph
        readonly property real s: width / 12
        implicitWidth: Math.round(Type.bodyPixels * 0.9)
        implicitHeight: implicitWidth
        preferredRendererType: Shape.CurveRenderer
        Accessible.ignored: true
        ShapePath {
            strokeColor: Theme.error
            strokeWidth: Math.max(1, glyph.s * 1.2)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: glyph.s * 6
            startY: glyph.s * 0.9
            PathArc { x: glyph.s * 6; y: glyph.s * 11.1; radiusX: glyph.s * 5.1; radiusY: glyph.s * 5.1 }
            PathArc { x: glyph.s * 6; y: glyph.s * 0.9; radiusX: glyph.s * 5.1; radiusY: glyph.s * 5.1 }
            PathMove { x: glyph.s * 6; y: glyph.s * 3.4 }
            PathLine { x: glyph.s * 6; y: glyph.s * 6.9 }
            PathMove { x: glyph.s * 6; y: glyph.s * 8.9 }
            PathLine { x: glyph.s * 6; y: glyph.s * 9.0 }
        }
    }
    // A section's name, once: "Arguments", "Request", "Response".
    component SectionHeading: Label {
        textFormat: Text.PlainText
        font.pointSize: Type.caption
        font.weight: Type.headingWeight
        color: Theme.textSecondary
        Accessible.role: Accessible.Heading
    }

    // Ctrl+Enter: Apply a draft, Execute a Command, Call a Service.
    function primaryAction() {
        if (!showSelection || !hasSelection || actionsComponent !== null)
            return false
        if (provider.commandSelected)
            return provider.canExecute ? (provider.executeCommand(), true) : false
        if (provider.serviceSelected)
            return provider.canCallService ? (provider.callService(), true) : false
        return provider.editing && provider.canApply ? (provider.apply(), true) : false
    }
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier))
            event.accepted = panel.primaryAction()
    }
    // The cell after or before row that takes text, opened; false at the end.
    function openNext(view, row, forward) {
        for (let r = row + (forward ? 1 : -1); r >= 0 && r < view.rows; r += forward ? 1 : -1) {
            view.positionViewAtRow(r, TableView.Contain)
            const item = view.itemAtCell(Qt.point(0, r))
            if (item && item.valueItem && item.valueItem.canOpen)
                return item.valueItem.begin()
        }
        view.forceActiveFocus()
        return false
    }
    // F2 or Enter opens the current row's cell.
    function openCurrent(view, event) {
        const editKey = event.key === Qt.Key_F2 || ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            && !(event.modifiers & Qt.ControlModifier))
        if (!editKey || view.currentRow < 0)
            return
        const item = view.itemAtCell(Qt.point(0, view.currentRow))
        if (item && item.valueItem && item.valueItem.canOpen)
            event.accepted = item.valueItem.begin()
    }
    // A Field picked while changes are allowed is read once, so its cells can
    // be edited at once: one read per selection, none while another is pending
    // and none for a Field already read. Anything the provider starts before
    // the turn ends (a Read pressed, a host's own request) takes its place.
    readonly property string selectedPath: hasSelection ? provider.selection : ""
    property bool readArmed: false
    onSelectedPathChanged: {
        resetColumns()
        readArmed = selectedPath.length > 0
        readTimer.restart()
    }
    readonly property bool providerBusy: provider.busy === true
    onProviderBusyChanged: if (providerBusy) readArmed = false
    Timer { id: readTimer; interval: 0; onTriggered: panel.readSelection() }
    function readSelection() {
        if (!readArmed)
            return
        readArmed = false
        if (!showSelection || !hasSelection || selectedCategory !== 1 || disconnected
            || provider.allowMutations !== true || selectionInfo.readable === false
            || provider.busy || provider.editing || !provider.canRefresh)
            return
        const values = provider.values
        if (values && typeof values.rowCount === "function" && values.rowCount() > 0)
            return
        provider.refresh()
    }
    // The rows a table shows: a struct's members without its root row (spec C:
    // "no root rows"); a scalar keeps its one row.
    function contentRoot(model) {
        if (!model || typeof model.index !== "function")
            return undefined
        return typeof model.contentRoot === "function" ? model.contentRoot() : model.index(-1, -1)
    }
    function fetchRoot(model) {
        const root = contentRoot(model)
        if (root && root.valid && model.canFetchMore(root))
            model.fetchMore(root)
    }
    Timer {
        id: rootFetch
        interval: 0
        onTriggered: {
            panel.fetchRoot(panel.provider.values)
            panel.fetchRoot(panel.provider.responses)
        }
    }

    component TableHeading: Item {
        required property var view
        implicitHeight: Type.rowHeight
        readonly property real tableWidth: view ? view.width : width
        Label {
            x: Type.padding + Type.indent
            width: Math.max(0, panel.nameEnd(parent.tableWidth) - x)
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Name"); color: Theme.textSecondary; font.pointSize: Type.caption
        }
        Label {
            x: panel.valueX(parent.tableWidth)
            width: panel.valueWidth(parent.tableWidth)
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Value"); color: Theme.textSecondary; font.pointSize: Type.caption
            horizontalAlignment: Text.AlignRight
        }
        Label {
            x: panel.typeX(parent.tableWidth)
            width: panel.typeWidth()
            visible: panel.typeShown(parent.tableWidth)
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Type"); color: Theme.textSecondary; font.pointSize: Type.caption
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.outlineVariant }
    }
    // One row of the family (spec A): Name, Value and Type at the same x at
    // every depth, striped, 22 px tall unless a cell's caption follows.
    component DetailRow: TreeViewDelegate {
        id: cellRow
        property string cellName: ""
        property string cellType: ""
        property string cellValue: ""
        property string cellHint: ""
        // A struct or array: its name in the heading weight.
        property bool container: false
        // A value not yet sent: the accent line at the row's left.
        property bool unsent: false
        property Component valueComponent
        // The row tooltip's name, so a host's check can find it per tree.
        property string toolTipName: ""
        readonly property var valueItem: valueCell.item
        readonly property real nameIndent: Math.min((depth + 1) * Type.indent,
            Math.max(Type.indent, panel.nameEnd(width) - Type.bodyPixels * 4 - Type.padding))
        indentation: Type.indent
        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
        indicator: TreeChevron { row: cellRow; x: Type.padding + cellRow.nameIndent - Type.indent }
        implicitWidth: treeView ? treeView.width : 0
        implicitHeight: Math.max(Type.rowHeight, valueCell.implicitHeight)
        text: cellName
        highlighted: current
        Accessible.name: cellName
        Accessible.description: [cellType, cellValue, cellHint].filter(function(part) { return part.length > 0 }).join("\n")
        readonly property real measuredValue: valueCell.item && typeof valueCell.item.textWidth === "number" ? valueCell.item.textWidth : 0
        onMeasuredValueChanged: panel.noteValue(measuredValue, valueCell.item && valueCell.item.numeric === true)
        // The row may be gone by the next turn of the event loop.
        onImplicitHeightChanged: Qt.callLater(function() { if (cellRow && cellRow.treeView) cellRow.treeView.forceLayout() })
        PlainToolTip { objectName: cellRow.toolTipName; visible: cellRow.hovered && cellRow.cellHint.length > 0; text: cellRow.cellHint }
        background: Rectangle {
            color: cellRow.highlighted
                ? (cellRow.treeView && cellRow.treeView.activeFocus ? Theme.stateSelected : Theme.stateSelectedInactive)
                : cellRow.hovered ? Theme.stateHover
                : cellRow.row % 2 === 1 ? Theme.surfaceAlternate : "transparent"
        }
        Rectangle {
            objectName: "unsentMark"
            width: 2; height: parent.height
            color: Theme.accent
            visible: cellRow.unsent
            Accessible.ignored: true
        }
        Rectangle {
            anchors.fill: parent; color: "transparent"
            border.width: 2; border.color: Theme.focusRing
            visible: cellRow.current && cellRow.treeView && cellRow.treeView.activeFocus
            Accessible.ignored: true
        }
        contentItem: Item {
            Label {
                objectName: "detailCellName"
                x: Type.padding + cellRow.nameIndent
                width: Math.max(0, panel.nameEnd(cellRow.width) - x)
                height: Type.rowHeight
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText; text: cellRow.cellName; elide: Text.ElideMiddle
                font.weight: cellRow.container ? Type.headingWeight : Type.bodyWeight
                color: cellRow.highlighted ? Theme.selectedForeground : Theme.textPrimary
            }
            Loader {
                id: valueCell
                x: panel.valueX(cellRow.width)
                width: panel.valueWidth(cellRow.width)
                sourceComponent: cellRow.valueComponent
            }
            Label {
                id: typeCellLabel
                objectName: "detailCellType"
                x: panel.typeX(cellRow.width)
                width: panel.typeWidth()
                height: Type.rowHeight
                visible: panel.typeShown(cellRow.width)
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText; text: cellRow.cellType; elide: Text.ElideRight
                font.family: Type.fixedFamily; font.pointSize: Type.caption
                color: cellRow.highlighted ? Theme.selectedForeground : Theme.textSecondary
                onImplicitWidthChanged: panel.noteType(implicitWidth)
                Component.onCompleted: panel.noteType(implicitWidth)
                HoverHandler { id: typeHover }
                PlainToolTip { visible: typeHover.hovered && typeCellLabel.truncated; text: cellRow.cellType }
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: Type.groupSpacing
        // The host's own header and footer follow the selection, not the
        // connection: they are its chrome, shown while the panel shows its
        // kind, connected or not.
        Loader { Layout.fillWidth: true; active: panel.selectionMatches && panel.headerComponent !== null; sourceComponent: panel.headerComponent }
        ColumnLayout {
            objectName: "endpointHeader"
            visible: panel.showSelection && panel.hasSelection
            Layout.fillWidth: true
            spacing: Type.tight
            GridLayout {
                id: selectionHeader
                Layout.fillWidth: true
                // Below 360 px the buttons take their own row, at the right.
                columns: width < 360 ? 1 : 2
                columnSpacing: Type.groupSpacing; rowSpacing: Type.tight
                Label {
                    id: titleLabel
                    objectName: "endpointSelectionTitle"
                    textFormat: Text.PlainText; text: panel.title
                    font.weight: Type.headingWeight; color: Theme.textPrimary
                    Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideMiddle
                    HoverHandler { id: titleHover }
                    PlainToolTip { visible: titleHover.hovered && titleLabel.truncated; text: panel.title }
                }
                Loader {
                    objectName: "endpointHostActions"
                    active: panel.actionsComponent !== null
                    visible: active
                    sourceComponent: panel.actionsComponent
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                }
                RowLayout {
                    id: selectionActions
                    objectName: "endpointSelectionActions"
                    visible: panel.actionsComponent === null
                    spacing: Type.groupSpacing
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    // The cells are the editor: a draft exists from the first
                    // change, and only then are there Cancel and Apply.
                    Button {
                        objectName: "refreshButton"; text: qsTr("Read")
                        visible: !panel.provider.commandSelected && !panel.provider.serviceSelected && !panel.provider.editing
                        enabled: panel.provider.canRefresh === true; onClicked: panel.provider.refresh()
                        ToolTip.visible: hovered; ToolTip.delay: 700; ToolTip.text: qsTr("Read this Field from the device")
                    }
                    Button {
                        objectName: "cancelEditButton"; text: qsTr("Cancel")
                        visible: panel.provider.editing === true && !panel.provider.commandSelected && !panel.provider.serviceSelected
                        enabled: !panel.provider.busy; onClicked: panel.provider.cancelEdit()
                        ToolTip.visible: hovered; ToolTip.delay: 700
                        ToolTip.text: qsTr("Put back the values read; nothing is written")
                    }
                    AccentButton {
                        objectName: "applyButton"; text: qsTr("Apply")
                        visible: panel.provider.editing === true && !panel.provider.commandSelected && !panel.provider.serviceSelected
                        enabled: panel.provider.canApply === true; onClicked: panel.provider.apply()
                        ToolTip.visible: hovered; ToolTip.delay: 700
                        ToolTip.text: qsTr("Write once, then read the device again (Ctrl+Enter). Undo does not take a write back.")
                        Accessible.description: ToolTip.text
                    }
                    AccentButton {
                        objectName: "executeButton"; text: qsTr("Execute")
                        visible: panel.provider.commandSelected === true; enabled: panel.provider.canExecute === true
                        onClicked: panel.provider.executeCommand()
                        ToolTip.visible: hovered; ToolTip.delay: 700
                        ToolTip.text: qsTr("Send the Command once (Ctrl+Enter). Undo does not take it back.")
                        Accessible.description: ToolTip.text
                    }
                    AccentButton {
                        objectName: "callServiceButton"; text: qsTr("Call")
                        visible: panel.provider.serviceSelected === true; enabled: panel.provider.canCallService === true
                        onClicked: panel.provider.callService()
                        ToolTip.visible: hovered; ToolTip.delay: 700
                        ToolTip.text: qsTr("Send one request (Ctrl+Enter). Undo does not take it back.")
                        Accessible.description: ToolTip.text
                    }
                }
            }
            // Work under way: one line, never a spinner.
            ProgressLine {
                objectName: "endpointProgress"
                Layout.fillWidth: true
                running: panel.providerBusy
                description: panel.statusInfo.text
            }
            Label {
                objectName: "endpointSelectionInfo"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: Theme.textMuted
                text: panel.selectionLine
                Accessible.description: panel.contextHelp
            }
        }
        // The status line: what happened last, or what is happening; a
        // failure has its glyph and the reason in the primary text colour.
        RowLayout {
            objectName: "endpointStatus"
            visible: (panel.showSelection && panel.hasSelection && panel.statusInfo.text.length > 0)
                || panel.retainedText.length > 0
            Layout.fillWidth: true
            Layout.topMargin: -Type.tight
            spacing: Type.spacing
            ErrorGlyph {
                objectName: "endpointStatusGlyph"
                visible: panel.retainedText.length === 0 && panel.statusInfo.failed === true
                Layout.alignment: Qt.AlignVCenter
            }
            Label {
                id: statusLine
                objectName: "endpointStatusLine"
                Layout.fillWidth: true
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pointSize: Type.caption
                text: panel.retainedText.length > 0 ? panel.retainedText : panel.statusInfo.text
                color: panel.retainedText.length === 0 && panel.statusInfo.failed === true ? Theme.textPrimary : Theme.textSecondary
                Accessible.description: typeof panel.statusInfo.tip === "string" ? panel.statusInfo.tip : ""
                HoverHandler { id: statusHover }
                PlainToolTip {
                    visible: statusHover.hovered && typeof panel.statusInfo.tip === "string" && panel.statusInfo.tip.length > 0
                    text: typeof panel.statusInfo.tip === "string" ? panel.statusInfo.tip : ""
                }
            }
        }
        // Why an action is off, with the way to turn it on: a routine state,
        // in the neutral container, read out as an alert.
        Rectangle {
            id: refusalStrip
            objectName: "endpointRefusal"
            visible: panel.refusalText.length > 0
            Layout.fillWidth: true
            implicitHeight: Math.max(Type.controlHeight, refusalRow.implicitHeight + 2 * Type.tight)
            radius: Type.radius
            color: Theme.neutralContainer
            Accessible.role: Accessible.AlertMessage
            Accessible.name: panel.refusalText
            RowLayout {
                id: refusalRow
                anchors.fill: parent
                anchors.leftMargin: Type.groupSpacing
                anchors.rightMargin: Type.tight
                spacing: Type.padding
                // No pictogram: a host that embeds this panel need not
                // register the library's C++ types (ToolIcon) for it.
                Label {
                    objectName: "endpointRefusalText"
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.textPrimary
                    text: panel.refusalText
                }
                Button {
                    objectName: "endpointAllowChanges"
                    text: qsTr("Allow changes")
                    flat: true
                    enabled: !panel.provider.busy
                    // A provider may refuse, or keep the switch to itself.
                    onClicked: {
                        try {
                            panel.provider.allowMutations = true
                        } catch (error) {
                            console.warn("Changes could not be allowed: " + error)
                        }
                    }
                }
            }
        }
        GridLayout {
            id: valueColumns
            visible: panel.showSelection && panel.hasSelection
            Layout.fillWidth: true
            Layout.fillHeight: !panel.showResponse || responseSection.hasRows
            columns: panel.showResponse && responseSection.hasRows && width >= panel.minimumTableWidth * 2 + Type.groupSpacing ? 2 : 1
            columnSpacing: Type.groupSpacing; rowSpacing: Type.groupSpacing
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumWidth: 160
                Layout.minimumHeight: Type.rowHeight * 3
                // The heading and caption above the table count too, so a
                // request's rows keep their whole height before its response.
                readonly property real headingsHeight: (valueSectionHeading.visible ? valueSectionHeading.implicitHeight + spacing : 0)
                    + (valueSectionCaption.visible ? valueSectionCaption.implicitHeight + spacing : 0)
                    + (noArgumentsLabel.visible ? noArgumentsLabel.implicitHeight + spacing : 0)
                Layout.preferredHeight: panel.showResponse && !responseSection.hasRows
                    ? headingsHeight + Math.min(Type.rowHeight * 10 + valueHeading.implicitHeight,
                        Math.max(Type.rowHeight * 3, valueTree.contentHeight) + valueHeading.implicitHeight)
                    : headingsHeight + Type.rowHeight * 8
                Layout.preferredWidth: valueColumns.width / valueColumns.columns
                spacing: Type.tight
                SectionHeading {
                    id: valueSectionHeading
                    objectName: "valueSectionHeading"
                    visible: panel.provider.commandSelected || panel.provider.serviceSelected
                    text: panel.provider.serviceSelected ? qsTr("Request") : qsTr("Arguments")
                }
                Label {
                    id: valueSectionCaption
                    objectName: "valueSectionCaption"
                    visible: (panel.provider.commandSelected || panel.provider.serviceSelected) && !panel.noArguments
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.pointSize: Type.caption
                    color: Theme.textMuted
                    text: qsTr("Start at zero, not at the device's defaults.")
                }
                Label {
                    id: noArgumentsLabel
                    objectName: "noArgumentsLabel"
                    visible: panel.noArguments
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    font.pointSize: Type.caption
                    color: Theme.textMuted
                    text: panel.provider.serviceSelected ? qsTr("Takes no request values.") : qsTr("No arguments.")
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    visible: !panel.noArguments
                    spacing: 0
                    TableHeading {
                        id: valueHeading
                        objectName: "valueTableHeader"
                        Layout.fillWidth: true
                        view: panel.showMetadata ? metadataLoader.item : valueTree
                    }
                    Loader {
                        id: metadataLoader
                        active: panel.showMetadata; visible: active
                        Layout.fillWidth: true; Layout.fillHeight: true
                        sourceComponent: Component {
                            TreeView {
                                id: metadataTree
                                objectName: "metadataTree"
                                // The Field's members, without the Field's own row. Set
                                // on the next turn: showing them loads the tree's rows,
                                // which must not happen inside a host's selection.
                                readonly property var definitionRoot: panel.provider.definitionRootIndex
                                function showDefinition() {
                                    rootIndex = definitionRoot
                                    fieldRoot.restart()
                                }
                                onDefinitionRootChanged: showDefinition()
                                // Two Fields of one group share their root.
                                Connections { target: panel; function onSelectedPathChanged() { metadataTree.showDefinition() } }
                                Component.onCompleted: fieldRoot.restart()
                                Timer {
                                    id: fieldRoot
                                    interval: 0
                                    onTriggered: {
                                        const root = metadataTree.definitionRoot
                                        const model = metadataTree.model
                                        const field = model ? model.index(0, 0, root) : undefined
                                        metadataTree.rootIndex = field && field.valid && model.hasChildren(field) ? field : root
                                    }
                                }
                                clip: true; model: panel.provider.definitions; rootIndex: definitionRoot
                                activeFocusOnTab: true; alternatingRows: false
                                selectionModel: ItemSelectionModel { model: metadataTree.model }
                                contentWidth: width
                                columnWidthProvider: function() { return metadataTree.width }
                                onWidthChanged: forceLayout()
                                delegate: DetailRow {
                                    id: metadataDelegate
                                    objectName: "endpointRow"
                                    required property string name
                                    required property string detail
                                    required property bool isEndpoint
                                    readonly property bool twoLines: false
                                    readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                                    cellName: name; cellType: typeText; cellValue: qsTr("Not read")
                                    cellHint: detail + "\n" + panel.contextHelp
                                    container: hasChildren
                                    valueComponent: Label {
                                        objectName: "metadataValueLabel"
                                        height: Type.rowHeight
                                        verticalAlignment: Text.AlignVCenter
                                        text: "–"; color: metadataDelegate.highlighted ? Theme.selectedForeground : Theme.textMuted
                                        horizontalAlignment: /^(?:[usi](?:8|16|32|64)|f(?:32|64)|bool)$/.test(metadataDelegate.typeText)
                                            ? Text.AlignRight : Text.AlignLeft
                                        Accessible.description: qsTr("Not read")
                                    }
                                }
                                ScrollBar.vertical: ScrollBar {}
                                Connections { target: Type; function onRowHeightChanged() { metadataTree.forceLayout() } }
                            }
                        }
                    }
                    TreeView {
                        id: valueTree
                        objectName: "valueTree"
                        visible: !panel.showMetadata
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; model: panel.provider.values; alternatingRows: false
                        rootIndex: {
                            void panel.valueModelRevision
                            return panel.contentRoot(panel.provider.values)
                        }
                        activeFocusOnTab: true
                        editTriggers: TableView.NoEditTriggers
                        Keys.onPressed: function(event) { panel.openCurrent(valueTree, event) }
                        selectionModel: ItemSelectionModel { model: valueTree.model }
                        contentWidth: width
                        columnWidthProvider: function() { return valueTree.width }
                        onWidthChanged: Qt.callLater(function() { if (valueTree) valueTree.forceLayout() })
                        Connections { target: Type; function onRowHeightChanged() { Qt.callLater(function() { if (valueTree) valueTree.forceLayout() }) } }
                        delegate: DetailRow {
                            id: valueDelegate
                            objectName: "valueRow"
                            toolTipName: "valueToolTip"
                            required property string name
                            required property string detail
                            required property string value
                            required property bool isAggregate
                            required property int childCount
                            required property int loadedCount
                            required property bool editable
                            required property string editorText
                            readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                            readonly property string canonicalText: typeof model.canonicalText === "string" ? model.canonicalText : ""
                            readonly property string editorKind: typeof model.editorKind === "string" ? model.editorKind : "text"
                            readonly property var editorChoices: model.editorChoices !== undefined ? model.editorChoices : []
                            readonly property bool choicesTruncated: model.editorChoicesTruncated === true
                            readonly property string takes: typeof model.takes === "string" ? model.takes : ""
                            readonly property bool stale: model.stale === true
                            readonly property string staleNote: typeof model.staleNote === "string" ? model.staleNote : ""
                            cellName: name; cellType: typeText; cellHint: detail; cellValue: value
                            container: isAggregate
                            unsent: model.unsent === true
                            TableView.onReused: if (valueItem && typeof valueItem.reset === "function") valueItem.reset()
                            valueComponent: ValueCell {
                                objectName: "valueCell"
                                name: valueDelegate.name
                                value: valueDelegate.value
                                typeText: valueDelegate.typeText
                                detail: valueDelegate.detail
                                isAggregate: valueDelegate.isAggregate
                                childCount: valueDelegate.childCount
                                loadedCount: valueDelegate.loadedCount
                                editable: valueDelegate.editable
                                editorText: valueDelegate.editorText
                                editorKind: valueDelegate.editorKind
                                editorChoices: valueDelegate.editorChoices
                                choicesTruncated: valueDelegate.choicesTruncated
                                canonicalText: valueDelegate.canonicalText
                                takes: valueDelegate.takes
                                stale: valueDelegate.stale
                                staleNote: valueDelegate.staleNote
                                highlighted: valueDelegate.highlighted
                                // Changes off: the value shows, nothing takes input,
                                // and the strip above says why.
                                locked: panel.changesOff || panel.provider.busy
                                setText: function(text) {
                                    return panel.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                                }
                                loadMore: function() {
                                    panel.provider.values.fetchMore(valueTree.modelIndex(Qt.point(0, valueDelegate.row)))
                                }
                                onTabbed: function(forward) { panel.openNext(valueTree, valueDelegate.row, forward) }
                                onFinished: function(byKey) {
                                    // A provider whose values are steps (the Batch's item)
                                    // starts the next change of this value as a new one.
                                    if (typeof panel.provider.closeValue === "function")
                                        panel.provider.closeValue()
                                    if (byKey)
                                        valueTree.forceActiveFocus()
                                }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                        Label {
                            objectName: "emptyValueLabel"
                            // Over the view, not in its (empty) content.
                            parent: valueTree
                            anchors.centerIn: parent
                            width: Math.max(0, parent.width - Type.padding * 2)
                            horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; textFormat: Text.PlainText
                            visible: valueTree.rows === 0
                            text: panel.provider.busy ? qsTr("Waiting…")
                                : panel.provider.serviceSelected || panel.provider.commandSelected ? qsTr("No arguments.")
                                : panel.hasSelection ? qsTr("Not read yet") : qsTr("Select a Field, Command or Service in the Device tree.")
                            color: Theme.textMuted
                        }
                    }
                }
            }
            ColumnLayout {
                id: responseSection
                visible: panel.showResponse
                readonly property bool hasRows: {
                    void panel.responseModelRevision
                    return !!panel.provider.responses && typeof panel.provider.responses.rowCount === "function"
                        && panel.provider.responses.rowCount() > 0
                }
                // A response from before the request changed: muted, with a note.
                readonly property bool stale: !!panel.provider.responses && panel.provider.responses.stale === true
                Layout.fillWidth: true; Layout.fillHeight: hasRows
                Layout.minimumWidth: 160
                Layout.minimumHeight: hasRows ? Type.rowHeight * 3 : Type.rowHeight
                Layout.preferredHeight: hasRows ? Type.rowHeight * 8 : Type.rowHeight
                Layout.preferredWidth: valueColumns.width / valueColumns.columns
                spacing: Type.tight
                SectionHeading {
                    objectName: "responseSectionHeading"
                    text: qsTr("Response")
                    Accessible.description: panel.provider.serviceResponseEndpoint
                }
                Label {
                    objectName: "responseStaleNote"
                    visible: responseSection.hasRows && responseSection.stale
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: typeof panel.provider.responses.staleNote === "string" ? panel.provider.responses.staleNote : ""
                    font.pointSize: Type.caption; color: Theme.textMuted
                }
                Label {
                    objectName: "emptyResponseLabel"
                    visible: !responseSection.hasRows
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: panel.provider.busy ? qsTr("Waiting…") : panel.emptyResponseText
                    font.pointSize: Type.caption; color: Theme.textMuted
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    visible: responseSection.hasRows
                    spacing: 0
                    TableHeading { objectName: "responseTableHeader"; Layout.fillWidth: true; view: responseTree }
                    TreeView {
                        id: responseTree
                        objectName: "responseTree"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; model: panel.provider.responses; alternatingRows: false
                        rootIndex: {
                            void panel.responseModelRevision
                            return panel.contentRoot(panel.provider.responses)
                        }
                        activeFocusOnTab: true
                        selectionModel: ItemSelectionModel { model: responseTree.model }
                        contentWidth: width
                        columnWidthProvider: function() { return responseTree.width }
                        onWidthChanged: Qt.callLater(function() { if (responseTree) responseTree.forceLayout() })
                        Connections { target: Type; function onRowHeightChanged() { Qt.callLater(function() { if (responseTree) responseTree.forceLayout() }) } }
                        delegate: DetailRow {
                            id: responseDelegate
                            objectName: "responseRow"
                            toolTipName: "responseToolTip"
                            required property string name
                            required property string detail
                            required property string value
                            required property bool isAggregate
                            required property int childCount
                            required property int loadedCount
                            readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                            readonly property bool stale: model.stale === true
                            readonly property string staleNote: typeof model.staleNote === "string" ? model.staleNote : ""
                            cellName: name; cellType: typeText; cellHint: stale && staleNote.length > 0 ? staleNote : detail
                            cellValue: value
                            container: isAggregate
                            valueComponent: ValueCell {
                                objectName: "responseCell"
                                name: responseDelegate.name
                                value: responseDelegate.value
                                typeText: responseDelegate.typeText
                                detail: responseDelegate.detail
                                isAggregate: responseDelegate.isAggregate
                                childCount: responseDelegate.childCount
                                loadedCount: responseDelegate.loadedCount
                                stale: responseDelegate.stale
                                staleNote: responseDelegate.staleNote
                                highlighted: responseDelegate.highlighted
                                loadMore: function() {
                                    panel.provider.responses.fetchMore(responseTree.modelIndex(Qt.point(0, responseDelegate.row)))
                                }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                    }
                }
            }
        }
        Label {
            objectName: "emptyEndpointLabel"
            visible: panel.showSelection && !panel.hasSelection
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: implicitHeight
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap; textFormat: Text.PlainText
            text: panel.emptyText
            color: Theme.textMuted
        }
        Item {
            visible: panel.showSelection && panel.showResponse && !responseSection.hasRows
            Layout.fillHeight: true
        }
        Loader { Layout.fillWidth: true; active: panel.selectionMatches && panel.footerComponent !== null; sourceComponent: panel.footerComponent }
        Item { visible: !panel.showSelection; Layout.fillHeight: true }
    }
    Label {
        anchors.centerIn: parent
        visible: !panel.showSelection && panel.retainedText.length === 0
        textFormat: Text.PlainText
        color: Theme.textMuted
        text: panel.categoryFilter === 1 ? qsTr("Select a Field") : panel.categoryFilter === 2 ? qsTr("Select a Command") : qsTr("Select a Service")
    }
    Connections {
        target: panel.provider.values
        function onModelReset() {
            ++panel.valueModelRevision
            rootFetch.restart()
            Qt.callLater(function() { if (valueTree) valueTree.forceLayout() })
        }
        function onRowsInserted() { ++panel.valueModelRevision }
        function onRowsRemoved() { ++panel.valueModelRevision }
    }
    Connections {
        target: panel.provider.responses
        function onModelReset() {
            ++panel.responseModelRevision
            rootFetch.restart()
            Qt.callLater(function() { if (responseTree) responseTree.forceLayout() })
        }
        function onRowsInserted() { ++panel.responseModelRevision }
        function onRowsRemoved() { ++panel.responseModelRevision }
    }
    Connections {
        target: panel.provider
        function onReadFinished(published) {
            ++panel.valueModelRevision
            if (published) {
                rootFetch.restart()
                Qt.callLater(function() { if (valueTree) valueTree.forceLayout() })
            }
        }
    }
}
