// Shared Field/Command/Service renderer; host-owned provider and I/O. With
// nothing selected it is one line; with a selection, the name and its actions
// on one row, a caption line for a draft's, a Command's or a Service's note,
// and the values a row high. `framed: false` drops the card for a dock. MIT.
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
    ColumnLayout {
        anchors.fill: parent
        spacing: Type.spacing
        Loader { Layout.fillWidth: true; active: panel.showSelection && panel.headerComponent !== null; sourceComponent: panel.headerComponent }
        GridLayout {
            id: selectionHeader
            Layout.fillWidth: true
            // Nothing selected: the value area's one line says what to do.
            visible: panel.showSelection && panel.hasSelection
            columns: width < 560 ? 1 : 2
            columnSpacing: Type.spacing
            rowSpacing: Type.tight
            Label {
                objectName: "endpointSelectionTitle"
                textFormat: Text.PlainText
                text: panel.provider.selection
                font.weight: Type.headingWeight
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }
            // One row beside the title when the panel is wide; in a narrow dock
            // the actions wrap under the title so every button stays inside it.
            Flow {
                id: selectionActions
                objectName: "endpointSelectionActions"
                spacing: Type.spacing
                readonly property real rowWidth: {
                    let total = 0, shown = 0
                    for (let i = 0; i < children.length; ++i) {
                        if (children[i].visible) {
                            total += children[i].implicitWidth
                            ++shown
                        }
                    }
                    return total + Math.max(0, shown - 1) * spacing
                }
                Layout.fillWidth: selectionHeader.columns === 1
                Layout.preferredWidth: selectionHeader.columns === 1 ? -1 : rowWidth
                BusyIndicator { running: panel.provider.busy; visible: running; implicitWidth: Type.controlHeight; implicitHeight: Type.controlHeight }
                Button {
                    objectName: "editButton"
                    text: "Edit"
                    visible: !panel.provider.editing && !panel.provider.commandSelected && !panel.provider.serviceSelected
                    enabled: panel.provider.canEdit
                    onClicked: panel.provider.beginEdit()
                }
                Button {
                    objectName: "cancelEditButton"
                    text: "Cancel"
                    visible: panel.provider.editing && !panel.provider.commandSelected && !panel.provider.serviceSelected
                    enabled: !panel.provider.busy
                    onClicked: panel.provider.cancelEdit()
                }
                Button {
                    objectName: "applyButton"
                    text: qsTr("Apply")
                    visible: panel.provider.editing && !panel.provider.commandSelected && !panel.provider.serviceSelected
                    enabled: panel.provider.canApply
                    onClicked: panel.provider.apply()
                    ToolTip.visible: hovered
                    ToolTip.delay: 700
                    ToolTip.text: qsTr("Write once, then read the device again")
                }
                Button {
                    objectName: "executeButton"
                    text: "Execute once"
                    visible: panel.provider.commandSelected
                    enabled: panel.provider.canExecute
                    onClicked: panel.provider.executeCommand()
                }
                Button {
                    objectName: "callServiceButton"
                    text: "Call once"
                    visible: panel.provider.serviceSelected
                    enabled: panel.provider.canCallService
                    onClicked: panel.provider.callService()
                }
                Button {
                    objectName: "refreshButton"
                    text: "Refresh"
                    visible: !panel.provider.commandSelected && !panel.provider.serviceSelected
                    enabled: panel.provider.canRefresh
                    onClicked: panel.provider.refresh()
                    ToolTip.visible: hovered
                    ToolTip.delay: 700
                    ToolTip.text: qsTr("Read the device again. The values shown are from the last completed read; arrays load in batches as you expand them.")
                }
            }
        }
        Label {
            objectName: "endpointSelectionInfo"
            visible: panel.showSelection && panel.hasSelection && text.length > 0
            Layout.fillWidth: true
            textFormat: Text.PlainText
            font.pointSize: Type.caption
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            text: {
                const info = panel.selectionInfo
                const parts = []
                if (typeof info.typeText === "string" && info.typeText.length > 0)
                    parts.push(info.typeText + (typeof info.responseTypeText === "string" && info.responseTypeText.length > 0
                        ? " → " + info.responseTypeText : ""))
                if (typeof info.accessText === "string" && info.accessText.length > 0)
                    parts.push(info.accessText)
                if (typeof info.wireBytes === "number" && info.wireBytes >= 0)
                    parts.push(qsTr("%L1 bytes").arg(info.wireBytes))
                return parts.join(" · ")
            }
        }
        // A draft's, a Command's or a Service's note, one caption line under the name.
        Label {
            objectName: "endpointNote"
            visible: panel.showSelection && panel.hasSelection && text.length > 0
            textFormat: Text.PlainText
            text: panel.provider.serviceSelected
                ? qsTr("Request values start at zero. Call sends one request; the response stays until the next accepted change or session.")
                : panel.provider.commandSelected
                ? qsTr("Arguments start at zero; the device's defaults may differ. Accepted does not confirm it ran.")
                : panel.provider.editing
                ? qsTr("A local draft. Apply writes it once and reads the device again.")
                : ""
            color: Theme.textMuted
            font.pointSize: Type.caption
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            objectName: "definitionUnreadNote"
            visible: panel.showMetadata
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: qsTr("Not read. Refresh reads the Field.")
            Accessible.description: qsTr("Expanding this definition does not read device values.")
            font.pointSize: Type.caption
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        GridLayout {
            id: valueColumns
            visible: panel.showSelection
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: panel.showResponse && width >= 650 ? 2 : 1
            Loader {
                active: panel.showMetadata
                visible: active
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 160
                sourceComponent: Component {
                    TreeView {
                        id: metadataTree
                        objectName: "metadataTree"
                        clip: true
                        model: panel.provider.definitions
                        rootIndex: panel.provider.definitionRootIndex
                        activeFocusOnTab: true
                        alternatingRows: false
                        selectionModel: ItemSelectionModel { model: metadataTree.model }
                        columnWidthProvider: function() { return Math.max(1, metadataTree.width) }
                        onWidthChanged: forceLayout()
                        delegate: EndpointTreeRow {
                            reserveWatchSpace: false
                            typeText: typeof model.typeText === "string" ? model.typeText : ""
                            accessText: typeof model.accessText === "string" ? model.accessText : ""
                            previewChild: model.previewChild === true
                            hint: qsTr("Not read. Refresh reads the Field; expanding its definition reads nothing.")
                        }
                        ScrollBar.vertical: ScrollBar {}
                        Connections {
                            target: Type
                            function onRowHeightChanged() { metadataTree.forceLayout() }
                        }
                    }
                }
            }
            TreeView {
                id: valueTree
                visible: !panel.showMetadata
                // A zoom changes the row height: the rows already laid out follow it.
                Connections {
                    target: Type
                    function onRowHeightChanged() { Qt.callLater(function() { if (valueTree) valueTree.forceLayout() }) }
                }
                objectName: "valueTree"
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Fits a 280 px dock (256 px of content); two columns appear only
                // from 650 px, where each is wider than this anyway.
                Layout.minimumWidth: 160
                Layout.preferredWidth: valueColumns.width / valueColumns.columns
                clip: true
                model: panel.provider.values
                Component.onCompleted: Qt.callLater(function() {
                    if (!valueTree) return
                    valueTree.forceLayout()
                    valueTree.expand(0)
                })
                columnWidthProvider: function() { return Math.max(1, valueTree.width) }
                onWidthChanged: Qt.callLater(function() { if (valueTree) valueTree.forceLayout() })
                delegate: TreeViewDelegate {
                    id: valueDelegate
                    indentation: Type.indent
                    indicator: TreeChevron { row: valueDelegate }
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
                    readonly property bool narrow: valueTree.width < Type.bodyPixels * 30
                    readonly property bool floatEditor: editable && (typeText === "f32" || typeText === "f64") && canonicalText.length > 0
                    TableView.onReused: {
                        scalarInput.inputValid = true
                        scalarInput.inputDirty = false
                        scalarEditorHost.manualCode = false
                    }
                    implicitWidth: valueTree.width
                    implicitHeight: Math.max(editable ? Type.controlHeight + Type.tight : Type.rowHeight, valueGrid.implicitHeight + Type.tight)
                    text: name
                    PlainToolTip {
                        objectName: "valueToolTip"
                        visible: valueDelegate.hovered && valueDelegate.detail.length > 0
                        text: valueDelegate.detail
                    }
                    contentItem: GridLayout {
                        id: valueGrid
                        columns: valueDelegate.narrow ? 1 : 3
                        columnSpacing: Type.groupSpacing
                        rowSpacing: Type.tight
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Label { textFormat: Text.PlainText; text: valueDelegate.name; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                            Label {
                                visible: valueDelegate.narrow && valueDelegate.typeText.length > 0
                                Layout.maximumWidth: Math.max(0, valueTree.width * 0.45)
                                Layout.minimumWidth: 0
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                text: valueDelegate.typeText
                                color: Theme.textSecondary
                                font.pointSize: Type.caption
                                font.family: Type.fixedFamily
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: valueDelegate.narrow
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: valueDelegate.narrow ? -1 : Math.min(230, Math.max(120, valueTree.width * 0.45))
                            spacing: Type.tight
                            Label {
                                visible: !valueDelegate.editable
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                text: valueDelegate.value
                                color: Theme.textPrimary
                                font.family: Type.fixedFamily
                                horizontalAlignment: valueDelegate.narrow ? Text.AlignLeft : Text.AlignRight
                                HoverHandler { id: scalarValueHover }
                                ToolTip.visible: scalarValueHover.hovered && truncated
                                ToolTip.text: valueDelegate.value
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
                                visible: valueDelegate.editable && (valueDelegate.floatEditor || !scalarInput.inputValid)
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                font.pointSize: Type.caption
                                color: scalarInput.inputValid ? Theme.textSecondary : Theme.error
                                text: scalarInput.inputValid
                                    ? qsTr("Encoded: %1 (%2)").arg(valueDelegate.canonicalText).arg(valueDelegate.typeText)
                                    : qsTr("Enter a valid value.")
                            }
                        }
                        Label {
                            objectName: "valueType"
                            visible: !valueDelegate.narrow && valueDelegate.typeText.length > 0
                            Layout.preferredWidth: Type.bodyPixels * 6
                            Layout.maximumWidth: Type.bodyPixels * 6
                            Layout.minimumWidth: 0
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            text: valueDelegate.typeText
                            color: Theme.textSecondary
                            font.family: Type.fixedFamily
                            font.pointSize: Type.caption
                            HoverHandler { id: valueTypeHover }
                            ToolTip.visible: valueTypeHover.hovered && truncated
                            ToolTip.text: valueDelegate.typeText
                        }
                        Button {
                            visible: isAggregate && childCount > 128
                            Layout.columnSpan: valueGrid.columns
                            enabled: loadedCount < childCount
                            text: "Load next 128 · " + loadedCount + "/" + childCount
                            onClicked: panel.provider.values.fetchMore(valueTree.modelIndex(Qt.point(0, row)))
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
                Label {
                    parent: valueTree
                    objectName: "emptyValueLabel"
                    textFormat: Text.PlainText
                    anchors.centerIn: parent
                    width: Math.max(0, parent.width - 24)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: valueTree.rows === 0
                    text: panel.provider.busy
                        ? panel.provider.serviceSelected ? "Waiting for the Service result…" : panel.provider.commandSelected ? "Waiting for the Command result…" : "Reading the selected Field…"
                        : panel.provider.serviceSelected ? "No argument values to edit. Call submits one Service request."
                        : panel.provider.commandSelected ? "No argument values to edit. Execute sends the Command once."
                        : panel.hasSelection ? qsTr("Not read yet. Refresh reads it.")
                        : qsTr("Select a Field, Command or Service in the Device tree.")
                    color: Theme.textSecondary
                }
            }
            ColumnLayout {
                visible: panel.showResponse
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Fits a 280 px dock (256 px of content); two columns appear only
                // from 650 px, where each is wider than this anyway.
                Layout.minimumWidth: 160
                Layout.preferredWidth: valueColumns.width / valueColumns.columns
                Label {
                    textFormat: Text.PlainText
                    text: panel.provider.serviceResponseEndpoint.length
                        ? "Last response · " + panel.provider.serviceResponseEndpoint : "Last response"
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }
                TreeView {
                    id: responseTree
                    // A zoom changes the row height: the rows already laid out follow it.
                    Connections {
                        target: Type
                        function onRowHeightChanged() { Qt.callLater(function() { if (responseTree) responseTree.forceLayout() }) }
                    }
                    objectName: "responseTree"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: panel.provider.responses
                    Component.onCompleted: Qt.callLater(function() {
                        if (!responseTree) return
                        responseTree.forceLayout()
                        responseTree.expand(0)
                    })
                    columnWidthProvider: function() { return Math.max(1, responseTree.width) }
                    onWidthChanged: Qt.callLater(function() { if (responseTree) responseTree.forceLayout() })
                    delegate: TreeViewDelegate {
                        id: responseDelegate
                        indentation: Type.indent
                        indicator: TreeChevron { row: responseDelegate }
                        objectName: "responseRow"
                        required property string name
                        required property string detail
                        required property string value
                        required property bool isAggregate
                        required property int childCount
                        required property int loadedCount
                        readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                        readonly property bool narrow: responseTree.width < Type.bodyPixels * 30
                        implicitWidth: responseTree.width
                        implicitHeight: Math.max(Type.rowHeight, responseGrid.implicitHeight + Type.tight)
                        text: name
                        PlainToolTip {
                            objectName: "responseToolTip"
                            visible: responseDelegate.hovered && responseDelegate.detail.length > 0
                            text: responseDelegate.detail
                        }
                        contentItem: GridLayout {
                            id: responseGrid
                            columns: responseDelegate.narrow ? 1 : 3
                            columnSpacing: Type.groupSpacing
                            rowSpacing: Type.tight
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Label { textFormat: Text.PlainText; text: responseDelegate.name; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0 }
                                Label {
                                    visible: responseDelegate.narrow && responseDelegate.typeText.length > 0
                                    Layout.maximumWidth: Math.max(0, responseTree.width * 0.45)
                                    Layout.minimumWidth: 0
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    text: responseDelegate.typeText
                                    color: Theme.textSecondary
                                    font.family: Type.fixedFamily
                                    font.pointSize: Type.caption
                                }
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: responseDelegate.value
                                elide: Text.ElideRight
                                Layout.minimumWidth: 0
                                Layout.fillWidth: responseDelegate.narrow
                                Layout.maximumWidth: responseDelegate.narrow ? responseTree.width : Math.max(120, responseTree.width * 0.45)
                                color: Theme.textPrimary
                                font.family: Type.fixedFamily
                                HoverHandler { id: responseValueHover }
                                ToolTip.visible: responseValueHover.hovered && truncated
                                ToolTip.text: responseDelegate.value
                            }
                            Label {
                                objectName: "responseType"
                                visible: !responseDelegate.narrow && responseDelegate.typeText.length > 0
                                Layout.preferredWidth: Type.bodyPixels * 6
                                Layout.maximumWidth: Type.bodyPixels * 6
                                Layout.minimumWidth: 0
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                text: responseDelegate.typeText
                                color: Theme.textSecondary
                                font.family: Type.fixedFamily
                                font.pointSize: Type.caption
                                HoverHandler { id: responseTypeHover }
                                ToolTip.visible: responseTypeHover.hovered && truncated
                                ToolTip.text: responseDelegate.typeText
                            }
                            Button {
                                visible: isAggregate && childCount > 128
                                Layout.columnSpan: responseGrid.columns
                                enabled: loadedCount < childCount
                                text: "Load next 128 · " + loadedCount + "/" + childCount
                                onClicked: panel.provider.responses.fetchMore(responseTree.modelIndex(Qt.point(0, row)))
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    Label {
                        parent: responseTree
                        objectName: "emptyResponseLabel"
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        width: Math.max(0, parent.width - 24)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        visible: responseTree.rows === 0
                        text: panel.provider.busy ? "Waiting for a response…" : "No owned response value."
                        color: Theme.textSecondary
                    }
                }
            }
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
            color: Theme.warning
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            textFormat: Text.PlainText
            objectName: "commandOutcomeLabel"
            visible: panel.outcomeVisible(2, panel.provider.commandOutcome)
            text: qsTr("Last command: %1").arg(panel.provider.commandOutcome)
            color: Theme.warning
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            textFormat: Text.PlainText
            objectName: "serviceOutcomeLabel"
            visible: panel.outcomeVisible(3, panel.provider.serviceOutcome)
            text: qsTr("Last service: %1").arg(panel.provider.serviceOutcome)
            color: Theme.warning
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
            Qt.callLater(function() {
                if (!responseTree) return
                responseTree.forceLayout()
                responseTree.expand(0)
            })
        }
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
