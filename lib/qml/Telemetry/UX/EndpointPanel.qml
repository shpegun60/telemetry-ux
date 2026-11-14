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
    function numericType(type) { return /^(?:[us](?:8|16|32|64)|f(?:32|64))$/.test(type) }
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
        readonly property var valueItem: valueCell.item
        // Deep DTOs keep both a usable disclosure target and a readable name.
        // Indentation stops inside Name instead of covering Value's editor.
        readonly property real nameIndent: Math.min((depth + 1) * Type.indent,
            Math.max(Type.indent, panel.nameWidth(width) - Type.bodyPixels * 4 - Type.padding * 2))
        indentation: Type.indent
        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
        indicator: TreeChevron { row: cellRow; x: cellRow.nameIndent - Type.indent }
        implicitWidth: panel.tableWidth(treeView)
        implicitHeight: Math.max(Type.rowHeight, valueCell.implicitHeight + Type.tight * 2)
        text: cellName
        highlighted: current
        Accessible.name: cellName
        Accessible.description: [cellType, cellValue, cellHint].filter(function(part) { return part.length > 0 }).join("\n")
        onImplicitHeightChanged: Qt.callLater(function() { if (cellRow.treeView) cellRow.treeView.forceLayout() })
        PlainToolTip { visible: cellRow.hovered && cellRow.cellHint.length > 0; text: cellRow.cellHint }
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
        Loader { Layout.fillWidth: true; active: panel.showSelection && panel.headerComponent !== null; sourceComponent: panel.headerComponent }
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
                Button {
                    objectName: "editButton"; text: qsTr("Edit")
                    visible: !panel.provider.editing && !panel.provider.commandSelected && !panel.provider.serviceSelected
                        && panel.selectionInfo.writable !== false
                    enabled: panel.provider.canEdit; onClicked: panel.provider.beginEdit()
                    ToolTip.visible: hovered; ToolTip.delay: 700
                    ToolTip.text: panel.showMetadata ? qsTr("Read the Field before editing")
                        : panel.provider.allowMutations === false ? qsTr("Enable writes to edit this Field") : qsTr("Edit a local draft")
                }
                Button {
                    objectName: "cancelEditButton"; text: qsTr("Cancel")
                    visible: panel.provider.editing && !panel.provider.commandSelected && !panel.provider.serviceSelected
                    enabled: !panel.provider.busy; onClicked: panel.provider.cancelEdit()
                }
                Button {
                    objectName: "applyButton"; text: qsTr("Apply")
                    visible: panel.provider.editing && !panel.provider.commandSelected && !panel.provider.serviceSelected
                    enabled: panel.provider.canApply; onClicked: panel.provider.apply()
                    ToolTip.visible: hovered; ToolTip.delay: 700
                    ToolTip.text: qsTr("Write once, then read the device again")
                }
                Button {
                    objectName: "executeButton"; text: qsTr("Execute once")
                    visible: panel.provider.commandSelected; enabled: panel.provider.canExecute
                    onClicked: panel.provider.executeCommand()
                }
                Button {
                    objectName: "callServiceButton"; text: qsTr("Call once")
                    visible: panel.provider.serviceSelected; enabled: panel.provider.canCallService
                    onClicked: panel.provider.callService()
                }
                Button {
                    objectName: "refreshButton"; text: panel.showMetadata ? qsTr("Read") : qsTr("Refresh")
                    visible: !panel.provider.commandSelected && !panel.provider.serviceSelected
                    enabled: panel.provider.canRefresh; onClicked: panel.provider.refresh()
                    ToolTip.visible: hovered; ToolTip.delay: 700; ToolTip.text: qsTr("Read this Field from the device")
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
            ToolButton {
                id: helpButton
                objectName: "endpointHelp"
                text: "?"
                implicitWidth: Type.controlHeight; implicitHeight: Type.controlHeight
                Accessible.name: qsTr("About this operation"); Accessible.description: panel.contextHelp
                ToolTip.visible: hovered; ToolTip.delay: 700; ToolTip.text: panel.contextHelp
                onClicked: operationHelp.open()
                Popup {
                    id: operationHelp
                    x: Math.min(0, helpButton.width - width)
                    y: helpButton.height
                    width: Math.min(360, panel.width - panel.padding * 2)
                    padding: Type.groupSpacing
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    contentItem: Label { textFormat: Text.PlainText; text: panel.contextHelp; wrapMode: Text.Wrap }
                }
            }
        }
        GridLayout {
            id: valueColumns
            visible: panel.showSelection
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
                                    text: "\u2014"; color: metadataDelegate.highlighted ? Theme.selectedForeground : Theme.textMuted
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
                    visible: !panel.showMetadata
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; model: panel.provider.values; alternatingRows: false
                    activeFocusOnTab: true
                    selectionModel: ItemSelectionModel { model: valueTree.model }
                    contentWidth: panel.tableWidth(valueTree)
                    Component.onCompleted: Qt.callLater(function() { if (valueTree) { valueTree.forceLayout(); valueTree.expand(0) } })
                    columnWidthProvider: function() { return panel.tableWidth(valueTree) }
                    onWidthChanged: Qt.callLater(function() { if (valueTree) valueTree.forceLayout() })
                    Connections { target: Type; function onRowHeightChanged() { Qt.callLater(function() { if (valueTree) valueTree.forceLayout() }) } }
                    delegate: DetailRow {
                        id: valueDelegate
                        objectName: "valueRow"
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
                        readonly property bool floatEditor: editable && (typeText === "f32" || typeText === "f64") && canonicalText.length > 0
                        cellName: name; cellType: typeText; cellHint: detail; cellValue: value
                        TableView.onReused: {
                            if (valueItem && typeof valueItem.resetEditor === "function") valueItem.resetEditor()
                        }
                        valueComponent: ColumnLayout {
                            function resetEditor() {
                                scalarInput.inputValid = true
                                scalarInput.inputDirty = false
                                scalarEditorHost.manualCode = false
                            }
                            spacing: Type.tight
                            Label {
                                id: scalarValueLabel
                                visible: !valueDelegate.editable
                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                textFormat: Text.PlainText; elide: Text.ElideRight
                                text: valueDelegate.value
                                color: valueDelegate.highlighted ? Theme.selectedForeground : valueDelegate.isAggregate ? Theme.textMuted : Theme.textPrimary
                                font.family: valueDelegate.isAggregate ? Qt.application.font.family : Type.fixedFamily
                                horizontalAlignment: panel.numericType(valueDelegate.typeText) ? Text.AlignRight : Text.AlignLeft
                                HoverHandler { id: scalarValueHover }
                                PlainToolTip { visible: scalarValueHover.hovered && scalarValueLabel.truncated; text: valueDelegate.value }
                            }
                            RowLayout {
                                id: scalarEditorHost
                                objectName: "valueEditor"
                                visible: valueDelegate.editable
                                Layout.fillWidth: true
                                spacing: Type.tight
                                property bool manualCode: false
                                readonly property bool choices: valueDelegate.editorKind === "bool" || valueDelegate.editorKind === "enum"
                                readonly property var options: {
                                    const rows = []
                                    for (let i = 0; i < valueDelegate.editorChoices.length; ++i) {
                                        const item = valueDelegate.editorChoices[i]
                                        if (item && typeof item.label === "string" && typeof item.text === "string")
                                            rows.push({ label: item.label, text: item.text, manual: false })
                                    }
                                    if (valueDelegate.editorKind === "enum" && !rows.some(function(item) { return item.text === valueDelegate.editorText }))
                                        rows.unshift({ label: (valueDelegate.choicesTruncated ? qsTr("Code (%1)") : qsTr("Unknown (%1)"))
                                            .arg(valueDelegate.editorText), text: valueDelegate.editorText, manual: false })
                                    if (valueDelegate.editorKind === "enum")
                                        rows.push({ label: qsTr("Enter code…"), text: "", manual: true })
                                    return rows
                                }
                                readonly property int optionIndex: {
                                    for (let i = 0; i < options.length; ++i)
                                        if (!options[i].manual && options[i].text === valueDelegate.editorText)
                                            return i
                                    return -1
                                }
                                ComboBox {
                                    id: valueChoice
                                    objectName: "valueChoice"
                                    palette.light: Theme.stateSelected
                                    palette.midlight: Theme.stateHover
                                    palette.mid: Theme.buttonFillPressed
                                    palette.dark: Theme.textSecondary
                                    palette.text: Theme.textPrimary
                                    palette.buttonText: Theme.textPrimary
                                    palette.highlight: Theme.stateSelected
                                    palette.highlightedText: Theme.selectedForeground
                                    visible: scalarEditorHost.choices && !scalarEditorHost.manualCode
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    textRole: "label"
                                    valueRole: "text"
                                    model: scalarEditorHost.options
                                    currentIndex: scalarEditorHost.optionIndex
                                    Accessible.name: qsTr("Value for %1").arg(valueDelegate.name)
                                    Accessible.description: valueDelegate.choicesTruncated
                                        ? qsTr("The list is limited. Choose Enter code to use another numeric code.")
                                        : valueDelegate.detail
                                    ToolTip.visible: hovered
                                    ToolTip.text: valueChoice.Accessible.description
                                    onActivated: function(index) {
                                        const item = scalarEditorHost.options[index]
                                        if (!item) return
                                        if (item.manual) {
                                            scalarEditorHost.manualCode = true
                                            codeFocusTimer.restart()
                                            return
                                        }
                                        scalarInput.inputValid = panel.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), item.text)
                                        if (scalarInput.inputValid)
                                            scalarInput.inputDirty = false
                                        currentIndex = Qt.binding(function() { return scalarEditorHost.optionIndex })
                                    }
                                }
                                TextField {
                                    id: scalarInput
                                    objectName: "scalarEditor"
                                    property string scalarName: valueDelegate.name
                                    property bool inputValid: true
                                    property bool inputDirty: false
                                    visible: !scalarEditorHost.choices || scalarEditorHost.manualCode
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.preferredHeight: Math.max(Type.controlHeight, implicitHeight)
                                    Binding on text {
                                        value: valueDelegate.editorText
                                        when: !scalarInput.activeFocus && !scalarInput.inputDirty
                                        restoreMode: Binding.RestoreNone
                                    }
                                    placeholderText: scalarEditorHost.manualCode ? qsTr("Numeric code") : ""
                                    font.family: Type.fixedFamily
                                    selectByMouse: true
                                    Accessible.name: qsTr("Value for %1").arg(valueDelegate.name)
                                    Accessible.description: valueDelegate.detail
                                    onTextEdited: {
                                        inputDirty = true
                                        inputValid = panel.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                                    }
                                    onEditingFinished: if (visible && inputDirty)
                                        inputValid = panel.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                                }
                                ToolButton {
                                    id: choiceReturn
                                    objectName: "valueChoiceReturn"
                                    visible: scalarEditorHost.manualCode
                                    text: "⌄"
                                    Accessible.name: qsTr("Choose a named value for %1").arg(valueDelegate.name)
                                    Accessible.description: qsTr("Discard the entered code and choose a named value.")
                                    ToolTip.visible: hovered
                                    ToolTip.text: choiceReturn.Accessible.description
                                    onClicked: {
                                        scalarInput.inputValid = panel.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), valueDelegate.editorText)
                                        if (!scalarInput.inputValid) return
                                        scalarEditorHost.manualCode = false
                                        scalarInput.inputDirty = false
                                        valueChoice.forceActiveFocus()
                                        valueChoice.popup.open()
                                    }
                                }
                                Timer {
                                    id: codeFocusTimer
                                    interval: 0
                                    onTriggered: if (scalarEditorHost.manualCode && scalarInput.visible) {
                                        scalarInput.forceActiveFocus()
                                        scalarInput.selectAll()
                                    }
                                }
                            }
                            Label {
                                objectName: "valueEncodedCaption"
                                visible: valueDelegate.editable && (!scalarInput.inputValid ||
                                    (valueDelegate.floatEditor && scalarInput.inputDirty && scalarInput.text !== valueDelegate.canonicalText))
                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                textFormat: Text.PlainText; wrapMode: Text.Wrap
                                font.pointSize: Type.caption
                                color: scalarInput.inputValid ? Theme.textSecondary : Theme.error
                                text: scalarInput.inputValid
                                    ? qsTr("Encoded: %1 (%2)").arg(valueDelegate.canonicalText).arg(valueDelegate.typeText)
                                    : qsTr("Enter a valid value.")
                            }
                            Button {
                                visible: valueDelegate.isAggregate && valueDelegate.childCount > 128
                                enabled: valueDelegate.loadedCount < valueDelegate.childCount
                                text: qsTr("Load next 128 \u00b7 %1/%2").arg(valueDelegate.loadedCount).arg(valueDelegate.childCount)
                                onClicked: panel.provider.values.fetchMore(valueTree.modelIndex(Qt.point(0, valueDelegate.row)))
                            }
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
                            : panel.provider.serviceSelected || panel.provider.commandSelected ? qsTr("No arguments")
                            : panel.hasSelection ? qsTr("Not read") : qsTr("Select an endpoint in the Device tree")
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
                        text: panel.provider.busy ? qsTr("Waiting\u2026") : qsTr("No response")
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
                        required property string name
                        required property string detail
                        required property string value
                        required property bool isAggregate
                        required property int childCount
                        required property int loadedCount
                        readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                        cellName: name; cellType: typeText; cellHint: detail; cellValue: value
                        valueComponent: ColumnLayout {
                            spacing: Type.tight
                            Label {
                                id: responseValueLabel
                                textFormat: Text.PlainText; text: responseDelegate.value; elide: Text.ElideRight
                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                color: responseDelegate.highlighted ? Theme.selectedForeground : responseDelegate.isAggregate ? Theme.textMuted : Theme.textPrimary
                                font.family: responseDelegate.isAggregate ? Qt.application.font.family : Type.fixedFamily
                                horizontalAlignment: panel.numericType(responseDelegate.typeText) ? Text.AlignRight : Text.AlignLeft
                                HoverHandler { id: responseValueHover }
                                PlainToolTip { visible: responseValueHover.hovered && responseValueLabel.truncated; text: responseDelegate.value }
                            }
                            Button {
                                visible: responseDelegate.isAggregate && responseDelegate.childCount > 128
                                enabled: responseDelegate.loadedCount < responseDelegate.childCount
                                text: qsTr("Load next 128 \u00b7 %1/%2").arg(responseDelegate.loadedCount).arg(responseDelegate.childCount)
                                onClicked: panel.provider.responses.fetchMore(responseTree.modelIndex(Qt.point(0, responseDelegate.row)))
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    ScrollBar.horizontal: ScrollBar { policy: panel.minimumTableWidth > responseTree.width + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                }
            }
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
        Loader { Layout.fillWidth: true; active: panel.showSelection && panel.footerComponent !== null; sourceComponent: panel.footerComponent }
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
