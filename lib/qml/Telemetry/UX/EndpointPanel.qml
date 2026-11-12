// Shared Field/Command/Service renderer; host-owned provider and I/O. With
// nothing selected it is one line; with a selection, the name and its actions
// on one row, a caption line for a draft's, a Command's or a Service's note,
// and the values a row high. `framed: false` drops the card for a dock. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    readonly property bool showResponse: (categoryFilter === 0 || categoryFilter === 3) && (provider.serviceSelected || provider.serviceResponseEndpoint.length > 0)
    background: Rectangle {
        color: panel.framed ? Theme.surface : Theme.surfaceWindow
        radius: panel.framed ? Type.radius * 2 : 0
    }
    ColumnLayout {
        anchors.fill: parent
        visible: panel.selectionMatches
        spacing: Type.spacing
        Loader { Layout.fillWidth: true; active: panel.headerComponent !== null; sourceComponent: panel.headerComponent }
        GridLayout {
            id: selectionHeader
            Layout.fillWidth: true
            // Nothing selected: the value area's one line says what to do.
            visible: panel.hasSelection
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
                    visible: panel.provider.editing
                    enabled: !panel.provider.busy
                    onClicked: panel.provider.cancelEdit()
                }
                Button {
                    objectName: "applyButton"
                    text: qsTr("Apply")
                    visible: panel.provider.editing
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
        // A draft's, a Command's or a Service's note, one caption line under the name.
        Label {
            objectName: "endpointNote"
            visible: panel.hasSelection && text.length > 0
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
        GridLayout {
            id: valueColumns
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: panel.showResponse && width >= 650 ? 2 : 1
            TreeView {
                id: valueTree
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
                    objectName: "valueRow"
                    required property string name
                    required property string detail
                    required property string value
                    required property bool isAggregate
                    required property int childCount
                    required property int loadedCount
                    required property bool editable
                    required property string editorText
                    TableView.onReused: scalarInput.inputValid = true
                    implicitWidth: valueTree.width
                    // A row with an editor is as tall as the editor, a control and a little room.
                    implicitHeight: editable ? Type.controlHeight + Type.tight : Type.rowHeight
                    text: name
                    PlainToolTip {
                        objectName: "valueToolTip"
                        visible: valueDelegate.hovered && valueDelegate.detail.length > 0
                        text: valueDelegate.detail
                    }
                    contentItem: RowLayout {
                        Label { textFormat: Text.PlainText; text: name; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { textFormat: Text.PlainText; visible: !editable; text: value; color: Theme.textPrimary; font.family: Type.fixedFamily }
                        TextField {
                            id: scalarInput
                            objectName: "scalarEditor"
                            property string scalarName: valueDelegate.name
                            property bool inputValid: true
                            visible: valueDelegate.editable
                            Layout.preferredWidth: Math.min(230, Math.max(120, valueTree.width - 120))
                            Layout.preferredHeight: Type.controlHeight
                            // Keep the operator's raw text while typing (for example
                            // the intermediate "4." in "4.25"). Invalid text also
                            // survives blur until corrected or the draft is cancelled.
                            Binding on text {
                                value: valueDelegate.editorText
                                when: !scalarInput.activeFocus && scalarInput.inputValid
                                restoreMode: Binding.RestoreNone
                            }
                            font.family: Type.fixedFamily
                            selectByMouse: true
                            onTextEdited: inputValid = panel.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                            onEditingFinished: inputValid = panel.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                        }
                        Button {
                            visible: isAggregate && childCount > 128
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
                        objectName: "responseRow"
                        required property string name
                        required property string detail
                        required property string value
                        required property bool isAggregate
                        required property int childCount
                        required property int loadedCount
                        implicitWidth: responseTree.width
                        implicitHeight: Type.rowHeight
                        text: name
                        PlainToolTip {
                            objectName: "responseToolTip"
                            visible: responseDelegate.hovered && responseDelegate.detail.length > 0
                            text: responseDelegate.detail
                        }
                        contentItem: RowLayout {
                            Label { textFormat: Text.PlainText; text: name; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { textFormat: Text.PlainText; text: value; color: Theme.textPrimary; font.family: Type.fixedFamily }
                            Button {
                                visible: isAggregate && childCount > 128
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
            textFormat: Text.PlainText
            objectName: "writeOutcomeLabel"
            visible: (panel.categoryFilter === 0 || panel.categoryFilter === 1) && panel.provider.writeOutcome.length > 0
            text: qsTr("Last write: %1").arg(panel.provider.writeOutcome)
            color: Theme.warning
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            textFormat: Text.PlainText
            objectName: "commandOutcomeLabel"
            visible: (panel.categoryFilter === 0 || panel.categoryFilter === 2) && panel.provider.commandOutcome.length > 0
            text: qsTr("Last command: %1").arg(panel.provider.commandOutcome)
            color: Theme.warning
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            textFormat: Text.PlainText
            objectName: "serviceOutcomeLabel"
            visible: (panel.categoryFilter === 0 || panel.categoryFilter === 3) && panel.provider.serviceOutcome.length > 0
            text: qsTr("Last service: %1").arg(panel.provider.serviceOutcome)
            color: Theme.warning
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Loader { Layout.fillWidth: true; active: panel.footerComponent !== null; sourceComponent: panel.footerComponent }
    }
    Label {
        anchors.centerIn: parent
        visible: !panel.selectionMatches
        textFormat: Text.PlainText
        text: panel.categoryFilter === 1 ? "Select a Field" : panel.categoryFilter === 2 ? "Select a Command" : "Select a Service"
    }
    Connections {
        target: panel.provider.values
        function onModelReset() {
            Qt.callLater(function() {
                if (!valueTree) return
                valueTree.forceLayout()
                valueTree.expand(0)
            })
        }
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
            if (published)
                Qt.callLater(function() {
                    if (!valueTree) return
                    valueTree.forceLayout()
                    valueTree.expand(0)
                })
        }
    }
}
