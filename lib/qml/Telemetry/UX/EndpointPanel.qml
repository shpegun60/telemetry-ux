// Field/Command/Service Details: schema and owned values in the same compact
// tree-table as Values. The host owns the provider, buffers and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
    property bool framed: true
    padding: framed ? Type.groupSpacing : Type.padding
    readonly property bool hasSelection: provider.selection.length > 0
    required property var provider
    property int categoryFilter: 0
    property Component headerComponent: null
    property Component footerComponent: null
    readonly property int selectedCategory: provider.commandSelected ? 2 : provider.serviceSelected ? 3 : 1
    readonly property bool selectionMatches: categoryFilter === 0 || (provider.selection.length > 0 && selectedCategory === categoryFilter)
    readonly property bool disconnected: typeof provider.connected === "boolean" && !provider.connected
    readonly property var selectionInfo: typeof provider.selectionInfo === "object" && provider.selectionInfo ? provider.selectionInfo : ({})
    property int valueModelRevision: 0
    property int responseModelRevision: 0
    // Changes are off: what the selection would need them for, in words.
    readonly property bool changesOff: typeof provider.allowMutations === "boolean" && !provider.allowMutations
    readonly property string refusalText: !showSelection || !hasSelection || disconnected || !changesOff ? ""
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
    readonly property bool hasOutcome: outcomeVisible(1, provider.writeOutcome)
        || outcomeVisible(2, provider.commandOutcome) || outcomeVisible(3, provider.serviceOutcome)
    readonly property bool showResponse: (categoryFilter === 0 || categoryFilter === 3) && provider.serviceSelected &&
        !disconnected && (provider.serviceResponseEndpoint.length === 0 || provider.serviceResponseEndpoint === provider.selection)
    function outcomeVisible(category, text) {
        return (categoryFilter === 0 || categoryFilter === category) && text.length > 0
            && (disconnected || (hasSelection && selectionMatches && selectedCategory === category))
    }
    background: Rectangle {
        color: panel.framed ? Theme.surface : Theme.surfaceWindow
        radius: panel.framed ? Type.radius * 2 : 0
    }
    readonly property real minimumTableWidth: Type.bodyPixels * 27
    function tableWidth(view) { return Math.max(minimumTableWidth, view ? view.width : 0) }
    function typeWidth() { return Type.bodyPixels * 5 }
    function valueWidth(width) { return Math.max(Type.bodyPixels * 14, width * 0.48) }
    function nameWidth(width) { return width - typeWidth() - valueWidth(width) }
    // Numbers and true/false stand right, as the Device tree's do.
    function numericType(type) { return /^(?:[usi](?:8|16|32|64)|f(?:32|64)|bool)$/.test(type) }
    function outcomeColor(needsAttention) {
        return typeof needsAttention === "boolean" && !needsAttention ? Theme.textSecondary : Theme.warning
    }
    readonly property string contextHelp: provider.serviceSelected
        ? qsTr("Request values start at zero, not at device defaults. Call sends one request. The response is kept until the next accepted change or session.")
        : provider.commandSelected
        ? qsTr("Arguments start at zero, not at device defaults. Execute sends the Command once. Accepted does not confirm that the device completed it.")
        : provider.editing
        ? qsTr("You are editing a local draft. Apply writes it once, then reads the device again. Floating-point values show the exact encoded value below the input.")
        : qsTr("The type and structure come from the Descriptor. Refresh reads the Field; expanding its definition does not send requests.")

    // The panel's one primary action: Apply, Execute or Call.
    component AccentButton: Button {
        font.weight: Type.headingWeight
        palette.button: Theme.accent
        palette.buttonText: Theme.accentForeground
        palette.brightText: Theme.accentForeground
        palette.disabled.button: Theme.surfaceDisabled
        palette.disabled.buttonText: Theme.textDisabled
    }
    // Ctrl+Enter: Apply a draft, Execute a Command, Call a Service.
    function primaryAction() {
        if (!showSelection || !hasSelection)
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
    // and none for a Field already read.
    // Anything the provider starts before the turn ends (a Read pressed, a
    // host's own request) takes the read's place.
    readonly property string selectedPath: hasSelection ? provider.selection : ""
    property bool readArmed: false
    onSelectedPathChanged: {
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

    component TableHeading: Item {
        required property var view
        implicitHeight: Math.max(24, Type.rowHeight)
        clip: true
        Item {
            x: view ? -view.contentX : 0
            width: panel.tableWidth(view)
            height: parent.height
            Label {
                x: Type.padding; width: panel.nameWidth(parent.width) - Type.padding * 2
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Name"); color: Theme.textSecondary; font.pointSize: Type.caption
            }
            Label {
                x: panel.nameWidth(parent.width) + Type.padding
                width: panel.valueWidth(parent.width) - Type.padding * 2
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Value"); color: Theme.textSecondary; font.pointSize: Type.caption
                horizontalAlignment: Text.AlignRight
            }
            Label {
                x: parent.width - panel.typeWidth() + Type.padding
                width: panel.typeWidth() - Type.padding * 2
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Type"); color: Theme.textSecondary; font.pointSize: Type.caption
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.outlineVariant }
    }
    // Indentation belongs to Name only. Value/Type line up at every depth;
    // the actual style's editor size determines the height of an editable row.
    component DetailRow: TreeViewDelegate {
        id: cellRow
        property string cellName: ""
        property string cellType: ""
        property string cellValue: ""
        property string cellHint: ""
        property Component valueComponent
        // The row tooltip's name, so a host's check can find it per tree.
        property string toolTipName: ""
        readonly property var valueItem: valueCell.item
        // Deep DTOs keep both a usable disclosure target and a readable name.
        // Indentation stops inside Name instead of covering Value's editor.
        readonly property real nameIndent: Math.min((depth + 1) * Type.indent,
            Math.max(Type.indent, panel.nameWidth(width) - Type.bodyPixels * 4 - Type.padding * 2))
        indentation: Type.indent
        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
        indicator: TreeChevron { row: cellRow; x: cellRow.nameIndent - Type.indent }
        implicitWidth: panel.tableWidth(treeView)
        implicitHeight: Math.max(Type.rowHeight, valueCell.implicitHeight)
        text: cellName
        highlighted: current
        Accessible.name: cellName
        Accessible.description: [cellType, cellValue, cellHint].filter(function(part) { return part.length > 0 }).join("\n")
        // The row may be gone by the next turn of the event loop.
        onImplicitHeightChanged: Qt.callLater(function() { if (cellRow && cellRow.treeView) cellRow.treeView.forceLayout() })
        PlainToolTip { objectName: cellRow.toolTipName; visible: cellRow.hovered && cellRow.cellHint.length > 0; text: cellRow.cellHint }
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
                width: Math.max(0, panel.nameWidth(cellRow.width) - x - Type.padding)
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText; text: cellRow.cellName; elide: Text.ElideRight
                color: cellRow.highlighted ? Theme.selectedForeground : Theme.textPrimary
            }
            Loader {
                id: valueCell
                x: panel.nameWidth(cellRow.width) + Type.padding
                width: Math.max(1, panel.valueWidth(cellRow.width) - Type.padding * 2)
                anchors.verticalCenter: parent.verticalCenter
                sourceComponent: cellRow.valueComponent
            }
            Label {
                id: typeCellLabel
                objectName: "detailCellType"
                x: cellRow.width - panel.typeWidth() + Type.padding
                width: panel.typeWidth() - Type.padding * 2
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText; text: cellRow.cellType; elide: Text.ElideRight
                font.family: Type.fixedFamily; font.pointSize: Type.caption
                color: cellRow.highlighted ? Theme.selectedForeground : Theme.textSecondary
                HoverHandler { id: typeHover }
                PlainToolTip { visible: typeHover.hovered && typeCellLabel.truncated; text: cellRow.cellType }
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: Type.spacing
        // The host's own header and footer follow the selection, not the
        // connection: they are its chrome, shown while the panel shows its
        // kind, connected or not.
        Loader { Layout.fillWidth: true; active: panel.selectionMatches && panel.headerComponent !== null; sourceComponent: panel.headerComponent }
        GridLayout {
            id: selectionHeader
            Layout.fillWidth: true
            visible: panel.showSelection && panel.hasSelection
            columns: width < 560 ? 1 : 2
            columnSpacing: Type.spacing; rowSpacing: Type.tight
            Label {
                objectName: "endpointSelectionTitle"
                textFormat: Text.PlainText; text: panel.provider.selection
                font.weight: Type.headingWeight; Layout.fillWidth: true; elide: Text.ElideMiddle
            }
            Flow {
                id: selectionActions
                objectName: "endpointSelectionActions"
                spacing: Type.spacing
                readonly property real rowWidth: {
                    let total = 0, shown = 0
                    for (let i = 0; i < children.length; ++i) {
                        if (children[i].visible) { total += children[i].implicitWidth; ++shown }
                    }
                    return total + Math.max(0, shown - 1) * spacing
                }
                Layout.fillWidth: selectionHeader.columns === 1
                Layout.preferredWidth: selectionHeader.columns === 1 ? -1 : rowWidth
                BusyIndicator { running: panel.provider.busy; visible: running; implicitWidth: Type.controlHeight; implicitHeight: Type.controlHeight }
                // The cells are the editor: a draft exists from the first
                // change, and only then are there Cancel and Apply.
                Button {
                    objectName: "refreshButton"; text: qsTr("Read")
                    visible: !panel.provider.commandSelected && !panel.provider.serviceSelected && !panel.provider.editing
                    enabled: panel.provider.canRefresh; onClicked: panel.provider.refresh()
                    ToolTip.visible: hovered; ToolTip.delay: 700; ToolTip.text: qsTr("Read this Field from the device")
                }
                Button {
                    objectName: "cancelEditButton"; text: qsTr("Cancel")
                    visible: panel.provider.editing && !panel.provider.commandSelected && !panel.provider.serviceSelected
                    enabled: !panel.provider.busy; onClicked: panel.provider.cancelEdit()
                    ToolTip.visible: hovered; ToolTip.delay: 700
                    ToolTip.text: qsTr("Put back the values read; nothing is written")
                }
                AccentButton {
                    objectName: "applyButton"; text: qsTr("Apply")
                    visible: panel.provider.editing && !panel.provider.commandSelected && !panel.provider.serviceSelected
                    enabled: panel.provider.canApply; onClicked: panel.provider.apply()
                    ToolTip.visible: hovered; ToolTip.delay: 700
                    ToolTip.text: qsTr("Write once, then read the device again (Ctrl+Enter)")
                }
                AccentButton {
                    objectName: "executeButton"; text: qsTr("Execute")
                    visible: panel.provider.commandSelected; enabled: panel.provider.canExecute
                    onClicked: panel.provider.executeCommand()
                    ToolTip.visible: hovered; ToolTip.delay: 700; ToolTip.text: qsTr("Execute once (Ctrl+Enter)")
                }
                AccentButton {
                    objectName: "callServiceButton"; text: qsTr("Call")
                    visible: panel.provider.serviceSelected; enabled: panel.provider.canCallService
                    onClicked: panel.provider.callService()
                    ToolTip.visible: hovered; ToolTip.delay: 700; ToolTip.text: qsTr("Call once (Ctrl+Enter)")
                }
            }
        }
        RowLayout {
            objectName: "endpointSelectionInfo"
            visible: panel.showSelection && panel.hasSelection
            Layout.fillWidth: true
            spacing: Type.groupSpacing
            Label {
                id: accessLabel
                objectName: "endpointAccess"
                readonly property string words: typeof panel.selectionInfo.accessText === "string" ? panel.selectionInfo.accessText : ""
                text: panel.selectedCategory === 1 && typeof panel.selectionInfo.readable === "boolean" && typeof panel.selectionInfo.writable === "boolean"
                    ? (panel.selectionInfo.readable ? "r" : "-") + (panel.selectionInfo.writable ? "w" : "-") : words
                visible: text.length > 0
                Accessible.name: words
                font.family: Type.fixedFamily
                font.pointSize: Type.caption; color: Theme.textSecondary
                HoverHandler { id: accessHover }
                PlainToolTip { visible: accessHover.hovered; text: accessLabel.words }
            }
            Label {
                text: typeof panel.selectionInfo.wireBytes === "number" && panel.selectionInfo.wireBytes >= 0
                    ? panel.selectionInfo.wireBytes === 1 ? qsTr("1 byte") : qsTr("%L1 bytes").arg(panel.selectionInfo.wireBytes) : ""
                visible: text.length > 0
                font.pointSize: Type.caption; color: Theme.textMuted
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "definitionUnreadNote"
                text: panel.showMetadata ? qsTr("Not read") : panel.provider.editing ? qsTr("Local draft") : ""
                visible: text.length > 0
                font.pointSize: Type.caption; color: Theme.textSecondary
                Accessible.description: panel.contextHelp
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
            columns: panel.showResponse && responseSection.hasRows && width >= panel.minimumTableWidth * 2 + Type.spacing ? 2 : 1
            columnSpacing: Type.groupSpacing; rowSpacing: Type.groupSpacing
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumWidth: 160
                Layout.minimumHeight: Type.rowHeight * 3
                Layout.preferredHeight: panel.showResponse && !responseSection.hasRows
                    ? Math.min(Type.rowHeight * 10 + valueHeading.implicitHeight,
                        Math.max(Type.rowHeight * 3, valueTree.contentHeight) + valueHeading.implicitHeight
                        + (panel.minimumTableWidth > valueTree.width + 1 ? valueHorizontal.implicitHeight : 0))
                    : Type.rowHeight * 8
                Layout.preferredWidth: valueColumns.width / valueColumns.columns
                spacing: 0
                Label {
                    objectName: "noArgumentsLabel"
                    visible: panel.noArguments
                    Layout.fillWidth: true
                    leftPadding: Type.padding
                    textFormat: Text.PlainText
                    font.pointSize: Type.caption
                    color: Theme.textMuted
                    text: qsTr("No arguments.")
                }
                TableHeading {
                    id: valueHeading
                    objectName: "valueTableHeader"
                    visible: !panel.noArguments
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
                            clip: true; model: panel.provider.definitions; rootIndex: panel.provider.definitionRootIndex
                            activeFocusOnTab: true; alternatingRows: false
                            selectionModel: ItemSelectionModel { model: metadataTree.model }
                            contentWidth: panel.tableWidth(metadataTree)
                            columnWidthProvider: function() { return panel.tableWidth(metadataTree) }
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
                                valueComponent: Label {
                                    objectName: "metadataValueLabel"
                                    text: "\u2014"; color: metadataDelegate.highlighted ? Theme.selectedForeground : Theme.textMuted
                                    horizontalAlignment: panel.numericType(metadataDelegate.typeText) ? Text.AlignRight : Text.AlignLeft
                                    Accessible.description: qsTr("Not read")
                                }
                            }
                            ScrollBar.vertical: ScrollBar {}
                            ScrollBar.horizontal: ScrollBar { policy: panel.minimumTableWidth > metadataTree.width + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                            Connections { target: Type; function onRowHeightChanged() { metadataTree.forceLayout() } }
                        }
                    }
                }
                TreeView {
                    id: valueTree
                    objectName: "valueTree"
                    visible: !panel.showMetadata && !panel.noArguments
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; model: panel.provider.values; alternatingRows: false
                    activeFocusOnTab: true
                    editTriggers: TableView.NoEditTriggers
                    Keys.onPressed: function(event) { panel.openCurrent(valueTree, event) }
                    selectionModel: ItemSelectionModel { model: valueTree.model }
                    contentWidth: panel.tableWidth(valueTree)
                    Component.onCompleted: Qt.callLater(function() { if (valueTree) { valueTree.forceLayout(); valueTree.expand(0) } })
                    columnWidthProvider: function() { return panel.tableWidth(valueTree) }
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
                        cellName: name; cellType: typeText; cellHint: detail; cellValue: value
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
                            onFinished: function(byKey) { if (byKey) valueTree.forceActiveFocus() }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    ScrollBar.horizontal: ScrollBar {
                        id: valueHorizontal
                        policy: panel.minimumTableWidth > valueTree.width + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    }
                    Label {
                        objectName: "emptyValueLabel"
                        anchors.centerIn: parent
                        width: Math.max(0, parent.width - Type.padding * 2)
                        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; textFormat: Text.PlainText
                        visible: valueTree.rows === 0
                        text: panel.provider.busy ? qsTr("Waiting\u2026")
                            : panel.provider.serviceSelected || panel.provider.commandSelected ? qsTr("No arguments.")
                            : panel.hasSelection ? qsTr("Not read") : qsTr("Select a Field, Command or Service in the Device tree.")
                        color: Theme.textMuted
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
                Layout.fillWidth: true; Layout.fillHeight: hasRows
                Layout.minimumWidth: 160
                Layout.minimumHeight: hasRows ? Type.rowHeight * 3 : Type.rowHeight
                Layout.preferredHeight: hasRows ? Type.rowHeight * 8 : Type.rowHeight
                Layout.preferredWidth: valueColumns.width / valueColumns.columns
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Response"); font.weight: Type.headingWeight; color: Theme.textSecondary
                        Accessible.description: panel.provider.serviceResponseEndpoint
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        visible: !responseSection.hasRows
                        objectName: "emptyResponseLabel"
                        text: panel.provider.busy ? qsTr("Waiting\u2026") : qsTr("Call to see the response.")
                        font.pointSize: Type.caption; color: Theme.textMuted
                    }
                }
                TableHeading { objectName: "responseTableHeader"; visible: responseSection.hasRows; Layout.fillWidth: true; view: responseTree }
                TreeView {
                    id: responseTree
                    objectName: "responseTree"
                    visible: responseSection.hasRows
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; model: panel.provider.responses; alternatingRows: false
                    activeFocusOnTab: true
                    selectionModel: ItemSelectionModel { model: responseTree.model }
                    contentWidth: panel.tableWidth(responseTree)
                    Component.onCompleted: Qt.callLater(function() { if (responseTree) { responseTree.forceLayout(); responseTree.expand(0) } })
                    columnWidthProvider: function() { return panel.tableWidth(responseTree) }
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
                        cellName: name; cellType: typeText; cellHint: detail; cellValue: value
                        valueComponent: ValueCell {
                            objectName: "responseCell"
                            name: responseDelegate.name
                            value: responseDelegate.value
                            typeText: responseDelegate.typeText
                            detail: responseDelegate.detail
                            isAggregate: responseDelegate.isAggregate
                            childCount: responseDelegate.childCount
                            loadedCount: responseDelegate.loadedCount
                            highlighted: responseDelegate.highlighted
                            loadMore: function() {
                                panel.provider.responses.fetchMore(responseTree.modelIndex(Qt.point(0, responseDelegate.row)))
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    ScrollBar.horizontal: ScrollBar { policy: panel.minimumTableWidth > responseTree.width + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
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
            text: qsTr("Select a Field, Command or Service in the Device tree.")
            color: Theme.textMuted
        }
        Item {
            visible: panel.showSelection && panel.showResponse && !responseSection.hasRows
            Layout.fillHeight: true
        }
        Label {
            visible: panel.disconnected && panel.hasOutcome
            text: qsTr("Previous connection")
            color: Theme.textMuted
            font.pointSize: Type.caption
            Layout.fillWidth: true
        }
        Label {
            textFormat: Text.PlainText
            objectName: "writeOutcomeLabel"
            visible: panel.outcomeVisible(1, panel.provider.writeOutcome)
            text: qsTr("Last write: %1").arg(panel.provider.writeOutcome)
            color: panel.outcomeColor(panel.provider.writeOutcomeNeedsAttention)
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            textFormat: Text.PlainText
            objectName: "commandOutcomeLabel"
            visible: panel.outcomeVisible(2, panel.provider.commandOutcome)
            text: qsTr("Last command: %1").arg(panel.provider.commandOutcome)
            color: panel.outcomeColor(panel.provider.commandOutcomeNeedsAttention)
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            textFormat: Text.PlainText
            objectName: "serviceOutcomeLabel"
            visible: panel.outcomeVisible(3, panel.provider.serviceOutcome)
            text: qsTr("Last service: %1").arg(panel.provider.serviceOutcome)
            color: panel.outcomeColor(panel.provider.serviceOutcomeNeedsAttention)
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Loader { Layout.fillWidth: true; active: panel.selectionMatches && panel.footerComponent !== null; sourceComponent: panel.footerComponent }
        Item { visible: !panel.showSelection; Layout.fillHeight: true }
    }
    Label {
        anchors.centerIn: parent
        visible: !panel.showSelection && !panel.hasOutcome
        textFormat: Text.PlainText
        text: panel.categoryFilter === 1 ? "Select a Field" : panel.categoryFilter === 2 ? "Select a Command" : "Select a Service"
    }
    Connections {
        target: panel.provider.values
        function onModelReset() {
            ++panel.valueModelRevision
            Qt.callLater(function() {
                if (!valueTree) return
                valueTree.forceLayout()
                valueTree.expand(0)
            })
        }
        function onRowsInserted() { ++panel.valueModelRevision }
        function onRowsRemoved() { ++panel.valueModelRevision }
    }
    Connections {
        target: panel.provider.responses
        function onModelReset() {
            ++panel.responseModelRevision
            Qt.callLater(function() {
                if (!responseTree) return
                responseTree.forceLayout()
                responseTree.expand(0)
            })
        }
        function onRowsInserted() { ++panel.responseModelRevision }
        function onRowsRemoved() { ++panel.responseModelRevision }
    }
    Connections {
        target: panel.provider
        function onReadFinished(published) {
            ++panel.valueModelRevision
            if (published)
                Qt.callLater(function() {
                    if (!valueTree) return
                    valueTree.forceLayout()
                    valueTree.expand(0)
                })
        }
    }
}
