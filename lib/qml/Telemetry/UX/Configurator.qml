// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Presentation only: the caller owns this provider, its models and all I/O.
Control {
    id: configurator
    required property var provider
    padding: 0
    background: Rectangle { color: configurator.palette.window }
    implicitWidth: contentItem.implicitWidth
    implicitHeight: contentItem.implicitHeight
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

    contentItem: ColumnLayout {
        spacing: 16
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            Pane {
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 220
                background: Rectangle { color: "#17202b"; radius: 10 }
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Label { text: "DEVICE ENDPOINTS"; color: "#99abbf"; font.pixelSize: 11; font.letterSpacing: 1 }
                    TreeView {
                        id: endpointTree
                        objectName: "endpointTree"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: configurator.provider.endpoints
                        columnWidthProvider: function() { return Math.max(1, endpointTree.width) }
                        onWidthChanged: Qt.callLater(function() { endpointTree.forceLayout() })
                        delegate: TreeViewDelegate {
                            id: endpointDelegate
                            objectName: "endpointRow"
                            required property string name
                            required property string detail
                            required property bool isEndpoint
                            implicitWidth: endpointTree.width
                            implicitHeight: 38
                            text: name
                            ToolTip.visible: hovered && detail.length > 0
                            ToolTip.text: detail
                            // Basic TreeViewDelegate reads model.display directly.
                            // A retired model may have no role data before its reset.
                            contentItem: Label {
                                text: endpointDelegate.name
                                elide: Text.ElideRight
                                color: endpointDelegate.highlighted
                                    ? endpointDelegate.palette.highlightedText
                                    : endpointDelegate.palette.buttonText
                            }
                            onClicked: {
                                if (isEndpoint)
                                    configurator.provider.selectEndpoint(endpointTree.modelIndex(Qt.point(0, row)))
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                        Component.onCompleted: Qt.callLater(function() {
                            endpointTree.forceLayout()
                            endpointTree.expandRecursively(0, 2)
                        })
                    }
                    Label { text: "Commands and Services have an explicit Request editor. Each action submits one request."; color: "#99abbf"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
                }
            }
            Pane {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
                background: Rectangle { color: "#17202b"; radius: 10 }
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 16
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: configurator.provider.selection.length ? configurator.provider.selection : "Select a Field"
                            font.pixelSize: 21
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                        BusyIndicator { running: configurator.provider.busy; visible: running; implicitWidth: 30; implicitHeight: 30 }
                        Button {
                            objectName: "editButton"
                            text: "Edit"
                            visible: !configurator.provider.editing && !configurator.provider.commandSelected && !configurator.provider.serviceSelected
                            enabled: configurator.provider.canEdit
                            onClicked: configurator.provider.beginEdit()
                        }
                        Button {
                            objectName: "cancelEditButton"
                            text: "Cancel"
                            visible: configurator.provider.editing
                            enabled: !configurator.provider.busy
                            onClicked: configurator.provider.cancelEdit()
                        }
                        Button {
                            objectName: "applyButton"
                            text: "Apply + Refresh"
                            visible: configurator.provider.editing
                            enabled: configurator.provider.canApply
                            onClicked: configurator.provider.apply()
                        }
                        Button {
                            objectName: "executeButton"
                            text: "Execute once"
                            visible: configurator.provider.commandSelected
                            enabled: configurator.provider.canExecute
                            onClicked: configurator.provider.executeCommand()
                        }
                        Button {
                            objectName: "callServiceButton"
                            text: "Call once"
                            visible: configurator.provider.serviceSelected
                            enabled: configurator.provider.canCallService
                            onClicked: configurator.provider.callService()
                        }
                        Button {
                            objectName: "refreshButton"
                            text: "Refresh"
                            visible: !configurator.provider.commandSelected && !configurator.provider.serviceSelected
                            enabled: configurator.provider.canRefresh
                            onClicked: configurator.provider.refresh()
                        }
                    }
                    GridLayout {
                        id: valueColumns
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: (configurator.provider.serviceSelected || configurator.provider.serviceResponseEndpoint.length > 0) && width >= 650 ? 2 : 1
                        TreeView {
                            id: valueTree
                            objectName: "valueTree"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 280
                            Layout.preferredWidth: valueColumns.width / valueColumns.columns
                            clip: true
                            model: configurator.provider.values
                            Component.onCompleted: Qt.callLater(function() {
                                valueTree.forceLayout()
                                valueTree.expand(0)
                            })
                            columnWidthProvider: function() { return Math.max(1, valueTree.width) }
                            onWidthChanged: Qt.callLater(function() { valueTree.forceLayout() })
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
                                ToolTip.visible: hovered && detail.length > 0
                                ToolTip.text: detail
                                contentItem: RowLayout {
                                    Label { text: name; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { visible: !editable; text: value; color: "#83e1ce"; font.family: "monospace" }
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
                                        onTextEdited: inputValid = configurator.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                                        onEditingFinished: inputValid = configurator.provider.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                                    }
                                    Button {
                                        visible: isAggregate && childCount > 128
                                        enabled: loadedCount < childCount
                                        text: "Load next 128 · " + loadedCount + "/" + childCount
                                        onClicked: configurator.provider.values.fetchMore(valueTree.modelIndex(Qt.point(0, row)))
                                    }
                                }
                            }
                            ScrollBar.vertical: ScrollBar {}
                            Label {
                                anchors.centerIn: parent
                                visible: valueTree.rows === 0
                                text: configurator.provider.busy
                                    ? configurator.provider.serviceSelected ? "Waiting for the Service result…" : configurator.provider.commandSelected ? "Waiting for the Command result…" : "Reading the selected Field…"
                                    : configurator.provider.serviceSelected ? "No argument values to edit. Call submits one Service request."
                                    : configurator.provider.commandSelected ? "No argument values to edit. Execute sends the Command once."
                                    : "Select a Field, then press Refresh."
                                color: "#99abbf"
                            }
                        }
                        ColumnLayout {
                            visible: configurator.provider.serviceSelected || configurator.provider.serviceResponseEndpoint.length > 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 280
                            Layout.preferredWidth: valueColumns.width / valueColumns.columns
                            Label {
                                text: configurator.provider.serviceResponseEndpoint.length
                                    ? "Last response · " + configurator.provider.serviceResponseEndpoint : "Last response"
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
                                model: configurator.provider.responses
                                Component.onCompleted: Qt.callLater(function() {
                                    responseTree.forceLayout()
                                    responseTree.expand(0)
                                })
                                columnWidthProvider: function() { return Math.max(1, responseTree.width) }
                                onWidthChanged: Qt.callLater(function() { responseTree.forceLayout() })
                                delegate: TreeViewDelegate {
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
                                    ToolTip.visible: hovered && detail.length > 0
                                    ToolTip.text: detail
                                    contentItem: RowLayout {
                                        Label { text: name; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Label { text: value; color: "#83e1ce"; font.family: "monospace" }
                                        Button {
                                            visible: isAggregate && childCount > 128
                                            enabled: loadedCount < childCount
                                            text: "Load next 128 · " + loadedCount + "/" + childCount
                                            onClicked: configurator.provider.responses.fetchMore(responseTree.modelIndex(Qt.point(0, row)))
                                        }
                                    }
                                }
                                ScrollBar.vertical: ScrollBar {}
                                Label {
                                    anchors.centerIn: parent
                                    visible: responseTree.rows === 0
                                    text: configurator.provider.busy ? "Waiting for a response…" : "No owned response value."
                                    color: "#99abbf"
                                }
                            }
                        }
                    }
                    Label {
                        text: configurator.provider.serviceSelected
                            ? "Initial request values are zero. Call submits one request; the read-only response is owned until a new accepted mutation or session change."
                            : configurator.provider.commandSelected
                            ? "Initial request values are zero; device defaults may differ. Accepted does not confirm execution."
                            : configurator.provider.editing
                            ? "Local draft only. Edit scalar values, then Apply to write once and read the device again."
                            : "Values belong to the last completed read. Arrays load in batches as you expand them."
                        color: "#99abbf"
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
        Pane {
            objectName: "watchlistPane"
            Layout.fillWidth: true
            Layout.preferredHeight: watchPanelExpanded.checked ? 220 : 54
            background: Rectangle { color: "#17202b"; radius: 10 }
            ColumnLayout {
                anchors.fill: parent
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        id: watchPanelExpanded
                        objectName: "watchlistExpanded"
                        text: "Watchlist " + configurator.provider.watchCount + "/32"
                        checked: false
                    }
                    Button {
                        objectName: "addWatchButton"
                        text: "Add selected Field"
                        enabled: configurator.provider.canAddWatch && !configurator.provider.watchActive
                        onClicked: {
                            if (configurator.provider.addWatch())
                                watchPanelExpanded.checked = true
                        }
                    }
                    Button {
                        objectName: "refreshWatchButton"
                        text: "Read once"
                        enabled: configurator.provider.connected && configurator.provider.watchCount > 0 && !configurator.provider.watchActive
                        onClicked: configurator.provider.refreshWatch()
                    }
                    Button {
                        objectName: "startWatchButton"
                        text: configurator.provider.watchActive ? "Stop" : "Start polling"
                        enabled: configurator.provider.watchActive || (configurator.provider.connected && configurator.provider.watchCount > 0)
                        onClicked: configurator.provider.watchActive ? configurator.provider.stopWatch() : configurator.provider.startWatch()
                    }
                    Label { text: configurator.provider.watchPending ? "Reading…" : configurator.provider.watchStatus; color: "#99abbf"; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: 330 }
                }
                RowLayout {
                    visible: watchPanelExpanded.checked
                    Layout.fillWidth: true
                    ComboBox {
                        objectName: "watchMode"
                        Layout.preferredWidth: 210
                        model: ["ReadMany", "Individual FieldRead"]
                        currentIndex: configurator.provider.watchIndividual ? 1 : 0
                        enabled: !configurator.provider.watchActive
                        onActivated: configurator.provider.watchIndividual = currentIndex === 1
                    }
                    Label { text: "Frequency"; color: "#99abbf" }
                    SpinBox {
                        objectName: "watchFrequency"
                        from: 1
                        to: 10
                        value: Math.max(1, Math.min(10, Math.round(1000 / configurator.provider.watchIntervalMs)))
                        enabled: !configurator.provider.watchActive
                        onValueModified: configurator.provider.watchIntervalMs = Math.round(1000 / value)
                    }
                    Label { text: "Hz · scalar Fields only · sequential samples"; color: "#99abbf"; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                    Button {
                        objectName: "clearWatchButton"
                        text: "Clear"
                        enabled: configurator.provider.watchCount > 0 && !configurator.provider.watchActive
                        onClicked: configurator.provider.clearWatch()
                    }
                }
                ListView {
                    id: watchList
                    objectName: "watchlistView"
                    visible: watchPanelExpanded.checked
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: configurator.provider.watchlist
                    clip: true
                    delegate: Item {
                        id: watchRow
                        objectName: "watchlistRow"
                        required property int index
                        required property string name
                        required property string status
                        required property string text
                        width: watchList.width
                        height: 34
                        RowLayout {
                            anchors.fill: parent
                            Label { text: watchRow.name; Layout.preferredWidth: Math.min(260, watchList.width / 3); elide: Text.ElideMiddle }
                            Label { text: watchRow.text; font.family: "monospace"; color: "#83e1ce"; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { text: watchRow.status; color: "#99abbf" }
                            Button {
                                objectName: "removeWatchButton"
                                property int watchRowIndex: watchRow.index
                                text: "Remove"
                                enabled: !configurator.provider.watchActive
                                onClicked: configurator.provider.removeWatch(watchRow.index)
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    Label { anchors.centerIn: parent; visible: configurator.provider.watchCount === 0; text: "Select a scalar Field and add it to the watchlist."; color: "#718297" }
                }
            }
        }
        Label {
            objectName: "writeOutcomeLabel"
            visible: configurator.provider.writeOutcome.length > 0
            text: configurator.provider.writeOutcome
            color: "#e6c57b"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "commandOutcomeLabel"
            visible: configurator.provider.commandOutcome.length > 0
            text: configurator.provider.commandOutcome
            color: "#e6c57b"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "serviceOutcomeLabel"
            visible: configurator.provider.serviceOutcome.length > 0
            text: configurator.provider.serviceOutcome
            color: "#e6c57b"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "statusLabel"
            text: configurator.provider.statusText
            color: configurator.provider.connected ? "#83e1ce" : "#99abbf"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
    }
    Connections {
        target: configurator.provider.values
        function onModelReset() {
            Qt.callLater(function() {
                valueTree.forceLayout()
                valueTree.expand(0)
            })
        }
    }
    Connections {
        target: configurator.provider.responses
        function onModelReset() {
            Qt.callLater(function() {
                responseTree.forceLayout()
                responseTree.expand(0)
            })
        }
    }
    Connections {
        target: configurator.provider.endpoints
        function onModelReset() {
            Qt.callLater(function() { endpointTree.expandRecursively(0, 2) })
        }
    }
    Connections {
        target: configurator.provider
        function onReadFinished(published) {
            if (published)
                Qt.callLater(function() {
                    valueTree.forceLayout()
                    valueTree.expand(0)
                })
        }
    }
}
