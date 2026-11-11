// Shared Field/Command/Service renderer; host-owned provider and I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: panel
    palette.window: "#11151c"
    palette.windowText: "#e8edf5"
    palette.base: "#17202b"
    palette.alternateBase: "#1c2836"
    palette.text: "#e8edf5"
    palette.button: "#233349"
    palette.buttonText: "#e8edf5"
    palette.highlight: "#215a68"
    palette.highlightedText: "#ffffff"
    palette.placeholderText: "#718297"
    required property var provider
    property int categoryFilter: 0
    property Component headerComponent: null
    property Component footerComponent: null
    readonly property int selectedCategory: provider.commandSelected ? 2 : provider.serviceSelected ? 3 : 1
    readonly property bool selectionMatches: categoryFilter === 0 || (provider.selection.length > 0 && selectedCategory === categoryFilter)
    readonly property bool showResponse: (categoryFilter === 0 || categoryFilter === 3) && (provider.serviceSelected || provider.serviceResponseEndpoint.length > 0)
    background: Rectangle { color: "#17202b"; radius: 10 }
    ColumnLayout {
        anchors.fill: parent
        visible: panel.selectionMatches
        spacing: 16
        Loader { Layout.fillWidth: true; active: panel.headerComponent !== null; sourceComponent: panel.headerComponent }
        GridLayout {
            Layout.fillWidth: true
            columns: width < 560 ? 1 : 2
            Label {
                objectName: "endpointSelectionTitle"
                textFormat: Text.PlainText
                text: panel.provider.selection.length ? panel.provider.selection : "Select a Field"
                font.pixelSize: 21
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }
            RowLayout {
                objectName: "endpointSelectionActions"
                BusyIndicator { running: panel.provider.busy; visible: running; implicitWidth: 30; implicitHeight: 30 }
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
                    text: "Apply + Refresh"
                    visible: panel.provider.editing
                    enabled: panel.provider.canApply
                    onClicked: panel.provider.apply()
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
                }
            }
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
                Layout.minimumWidth: 280
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
                    implicitHeight: 40
                    text: name
                    PlainToolTip {
                        objectName: "valueToolTip"
                        visible: valueDelegate.hovered && valueDelegate.detail.length > 0
                        text: valueDelegate.detail
                    }
                    contentItem: RowLayout {
                        Label { textFormat: Text.PlainText; text: name; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label { textFormat: Text.PlainText; visible: !editable; text: value; color: "#83e1ce"; font.family: "monospace" }
                        TextField {
                            id: scalarInput
                            objectName: "scalarEditor"
                            property string scalarName: valueDelegate.name
                            property bool inputValid: true
                            visible: valueDelegate.editable
                            Layout.preferredWidth: Math.min(230, Math.max(120, valueTree.width - 120))
                            // Keep the operator's raw text while typing (for example
                            // the intermediate "4." in "4.25"). Invalid text also
                            // survives blur until corrected or the draft is cancelled.
                            Binding on text {
                                value: valueDelegate.editorText
                                when: !scalarInput.activeFocus && scalarInput.inputValid
                                restoreMode: Binding.RestoreNone
                            }
                            font.family: "monospace"
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
                        : "Select a Field, then press Refresh."
                    color: "#99abbf"
                }
            }
            ColumnLayout {
                visible: panel.showResponse
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 280
                Layout.preferredWidth: valueColumns.width / valueColumns.columns
                Label {
                    textFormat: Text.PlainText
                    text: panel.provider.serviceResponseEndpoint.length
                        ? "Last response · " + panel.provider.serviceResponseEndpoint : "Last response"
                    color: "#99abbf"
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
                        implicitHeight: 40
                        text: name
                        PlainToolTip {
                            objectName: "responseToolTip"
                            visible: responseDelegate.hovered && responseDelegate.detail.length > 0
                            text: responseDelegate.detail
                        }
                        contentItem: RowLayout {
                            Label { textFormat: Text.PlainText; text: name; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { textFormat: Text.PlainText; text: value; color: "#83e1ce"; font.family: "monospace" }
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
                        color: "#99abbf"
                    }
                }
            }
        }
        Label {
            textFormat: Text.PlainText
            text: panel.provider.serviceSelected
                ? "Initial request values are zero. Call submits one request; the read-only response is owned until a new accepted mutation or session change."
                : panel.provider.commandSelected
                ? "Initial request values are zero; device defaults may differ. Accepted does not confirm execution."
                : panel.provider.editing
                ? "Local draft only. Edit scalar values, then Apply to write once and read the device again."
                : "Values belong to the last completed read. Arrays load in batches as you expand them."
            color: "#99abbf"
            font.pixelSize: 11
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            textFormat: Text.PlainText
            objectName: "writeOutcomeLabel"
            visible: (panel.categoryFilter === 0 || panel.categoryFilter === 1) && panel.provider.writeOutcome.length > 0
            text: qsTr("Last write: %1").arg(panel.provider.writeOutcome)
            color: "#e6c57b"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            textFormat: Text.PlainText
            objectName: "commandOutcomeLabel"
            visible: (panel.categoryFilter === 0 || panel.categoryFilter === 2) && panel.provider.commandOutcome.length > 0
            text: qsTr("Last command: %1").arg(panel.provider.commandOutcome)
            color: "#e6c57b"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            textFormat: Text.PlainText
            objectName: "serviceOutcomeLabel"
            visible: (panel.categoryFilter === 0 || panel.categoryFilter === 3) && panel.provider.serviceOutcome.length > 0
            text: qsTr("Last service: %1").arg(panel.provider.serviceOutcome)
            color: "#e6c57b"
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
