// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "inspectorWindow"
    visible: true
    width: 1080
    height: 720
    minimumWidth: 780
    minimumHeight: 480
    title: "Telemetry Inspector"
    color: "#11151c"
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Label { text: "TELEMETRY"; color: "#68d6c2"; font.pixelSize: 12; font.letterSpacing: 2 }
                Label { text: "Universal Inspector"; font.pixelSize: 28; font.weight: Font.DemiBold }
            }
            Item { Layout.fillWidth: true }
            Label { text: controller.editing || controller.commandSelected || controller.serviceSelected ? "LOCAL DRAFT" : controller.writesEnabled ? "CHANGES ENABLED" : "READ ONLY"; color: "#99abbf"; font.pixelSize: 12 }
            Button { text: "Demo device"; onClicked: controller.connectDemo() }
            Button { objectName: "writableDemoButton"; text: "Writable demo"; onClicked: controller.connectWritableDemo() }
            Button { text: "Disconnect"; enabled: controller.connected || controller.busy; onClicked: controller.disconnectDevice() }
        }
        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                text: "Enable Field writes, Commands and Services for this connection"
                checked: controller.writesEnabled
                enabled: controller.writeProfileSupported && !controller.busy
                onToggled: controller.writesEnabled = checked
            }
            Button {
                objectName: "readManyDemoButton"
                text: "ReadMany demo"
                onClicked: controller.connectReadManyDemo()
            }
            Label {
                visible: controller.connected && !controller.writeProfileSupported
                text: "This transport profile is read-only."
                color: "#99abbf"
                Layout.fillWidth: true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: tcpHost
                objectName: "tcpHost"
                Layout.fillWidth: true
                text: controller.tcpHost
                placeholderText: "TCP host"
                onAccepted: controller.connectTcp(text, tcpPort.value)
            }
            SpinBox {
                id: tcpPort
                objectName: "tcpPort"
                from: 1
                to: 65535
                value: controller.tcpPort
                editable: true
            }
            Button {
                text: "Connect TCP"
                enabled: tcpHost.text.length > 0 && !controller.busy
                onClicked: controller.connectTcp(tcpHost.text, tcpPort.value)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: controller.serialAvailable
            ComboBox {
                id: serialPort
                objectName: "serialPort"
                Layout.fillWidth: true
                editable: true
                model: controller.serialPorts()
                editText: controller.serialPort
            }
            Button {
                text: "Scan ports"
                onClicked: serialPort.model = controller.serialPorts()
            }
            SpinBox {
                id: serialBaud
                from: 1200
                to: 4000000
                value: controller.serialBaud
                editable: true
            }
            Button {
                text: "Connect UART"
                enabled: serialPort.editText.length > 0 && !controller.busy
                onClicked: controller.connectSerial(serialPort.editText, serialBaud.value)
            }
        }
        CheckBox {
            objectName: "readManyDiscoveryCheck"
            visible: controller.serialAvailable
            text: "Discover ReadMany on the next UART connection"
            checked: controller.readManyDiscovery
            onToggled: controller.readManyDiscovery = checked
        }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: descriptorPath
                objectName: "descriptorPath"
                Layout.fillWidth: true
                placeholderText: "Descriptor file path · metadata only"
                onAccepted: controller.openDescriptorPath(text)
            }
            Button {
                text: "Open Descriptor"
                enabled: descriptorPath.text.length > 0
                onClicked: controller.openDescriptorPath(descriptorPath.text)
            }
        }
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
                        model: controller.endpoints
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
                                    controller.selectEndpoint(endpointTree.modelIndex(Qt.point(0, row)))
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
                            text: controller.selection.length ? controller.selection : "Select a Field"
                            font.pixelSize: 21
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                        BusyIndicator { running: controller.busy; visible: running; implicitWidth: 30; implicitHeight: 30 }
                        Button {
                            objectName: "editButton"
                            text: "Edit"
                            visible: !controller.editing && !controller.commandSelected && !controller.serviceSelected
                            enabled: controller.canEdit
                            onClicked: controller.beginEdit()
                        }
                        Button {
                            objectName: "cancelEditButton"
                            text: "Cancel"
                            visible: controller.editing
                            enabled: !controller.busy
                            onClicked: controller.cancelEdit()
                        }
                        Button {
                            objectName: "applyButton"
                            text: "Apply + Refresh"
                            visible: controller.editing
                            enabled: controller.canApply
                            onClicked: controller.apply()
                        }
                        Button {
                            objectName: "executeButton"
                            text: "Execute once"
                            visible: controller.commandSelected
                            enabled: controller.canExecute
                            onClicked: controller.executeCommand()
                        }
                        Button {
                            objectName: "callServiceButton"
                            text: "Call once"
                            visible: controller.serviceSelected
                            enabled: controller.canCallService
                            onClicked: controller.callService()
                        }
                        Button {
                            objectName: "refreshButton"
                            text: "Refresh"
                            visible: !controller.commandSelected && !controller.serviceSelected
                            enabled: controller.canRefresh
                            onClicked: controller.refresh()
                        }
                    }
                    GridLayout {
                        id: valueColumns
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        columns: (controller.serviceSelected || controller.serviceResponseEndpoint.length > 0) && width >= 650 ? 2 : 1
                        TreeView {
                            id: valueTree
                            objectName: "valueTree"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 280
                            Layout.preferredWidth: valueColumns.width / valueColumns.columns
                            clip: true
                            model: controller.values
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
                                        onTextEdited: inputValid = controller.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                                        onEditingFinished: inputValid = controller.setScalarText(valueTree.modelIndex(Qt.point(0, valueDelegate.row)), text)
                                    }
                                    Button {
                                        visible: isAggregate && childCount > 128
                                        enabled: loadedCount < childCount
                                        text: "Load next 128 · " + loadedCount + "/" + childCount
                                        onClicked: controller.values.fetchMore(valueTree.modelIndex(Qt.point(0, row)))
                                    }
                                }
                            }
                            ScrollBar.vertical: ScrollBar {}
                            Label {
                                anchors.centerIn: parent
                                visible: valueTree.rows === 0
                                text: controller.busy
                                    ? controller.serviceSelected ? "Waiting for the Service result…" : controller.commandSelected ? "Waiting for the Command result…" : "Reading the selected Field…"
                                    : controller.serviceSelected ? "No argument values to edit. Call submits one Service request."
                                    : controller.commandSelected ? "No argument values to edit. Execute sends the Command once."
                                    : "Select a Field, then press Refresh."
                                color: "#99abbf"
                            }
                        }
                        ColumnLayout {
                            visible: controller.serviceSelected || controller.serviceResponseEndpoint.length > 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 280
                            Layout.preferredWidth: valueColumns.width / valueColumns.columns
                            Label {
                                text: controller.serviceResponseEndpoint.length
                                    ? "Last response · " + controller.serviceResponseEndpoint : "Last response"
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
                                model: controller.responses
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
                                            onClicked: controller.responses.fetchMore(responseTree.modelIndex(Qt.point(0, row)))
                                        }
                                    }
                                }
                                ScrollBar.vertical: ScrollBar {}
                                Label {
                                    anchors.centerIn: parent
                                    visible: responseTree.rows === 0
                                    text: controller.busy ? "Waiting for a response…" : "No owned response value."
                                    color: "#99abbf"
                                }
                            }
                        }
                    }
                    Label {
                        text: controller.serviceSelected
                            ? "Initial request values are zero. Call submits one request; the read-only response is owned until a new accepted mutation or session change."
                            : controller.commandSelected
                            ? "Initial request values are zero; device defaults may differ. Accepted does not confirm execution."
                            : controller.editing
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
                        text: "Watchlist " + controller.watchCount + "/32"
                        checked: false
                    }
                    Button {
                        objectName: "addWatchButton"
                        text: "Add selected Field"
                        enabled: controller.canAddWatch && !controller.watchActive
                        onClicked: {
                            if (controller.addWatch())
                                watchPanelExpanded.checked = true
                        }
                    }
                    Button {
                        objectName: "refreshWatchButton"
                        text: "Read once"
                        enabled: controller.connected && controller.watchCount > 0 && !controller.watchActive
                        onClicked: controller.refreshWatch()
                    }
                    Button {
                        objectName: "startWatchButton"
                        text: controller.watchActive ? "Stop" : "Start polling"
                        enabled: controller.watchActive || (controller.connected && controller.watchCount > 0)
                        onClicked: controller.watchActive ? controller.stopWatch() : controller.startWatch()
                    }
                    Label { text: controller.watchPending ? "Reading…" : controller.watchStatus; color: "#99abbf"; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: 330 }
                }
                RowLayout {
                    visible: watchPanelExpanded.checked
                    Layout.fillWidth: true
                    ComboBox {
                        objectName: "watchMode"
                        Layout.preferredWidth: 210
                        model: ["ReadMany", "Individual FieldRead"]
                        currentIndex: controller.watchIndividual ? 1 : 0
                        enabled: !controller.watchActive
                        onActivated: controller.watchIndividual = currentIndex === 1
                    }
                    Label { text: "Frequency"; color: "#99abbf" }
                    SpinBox {
                        objectName: "watchFrequency"
                        from: 1
                        to: 10
                        value: Math.max(1, Math.min(10, Math.round(1000 / controller.watchIntervalMs)))
                        enabled: !controller.watchActive
                        onValueModified: controller.watchIntervalMs = Math.round(1000 / value)
                    }
                    Label { text: "Hz · scalar Fields only · sequential samples"; color: "#99abbf"; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                    Button {
                        objectName: "clearWatchButton"
                        text: "Clear"
                        enabled: controller.watchCount > 0 && !controller.watchActive
                        onClicked: controller.clearWatch()
                    }
                }
                ListView {
                    id: watchList
                    objectName: "watchlistView"
                    visible: watchPanelExpanded.checked
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: controller.watchlist
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
                                enabled: !controller.watchActive
                                onClicked: controller.removeWatch(watchRow.index)
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    Label { anchors.centerIn: parent; visible: controller.watchCount === 0; text: "Select a scalar Field and add it to the watchlist."; color: "#718297" }
                }
            }
        }
        Label {
            objectName: "writeOutcomeLabel"
            visible: controller.writeOutcome.length > 0
            text: controller.writeOutcome
            color: "#e6c57b"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "commandOutcomeLabel"
            visible: controller.commandOutcome.length > 0
            text: controller.commandOutcome
            color: "#e6c57b"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "serviceOutcomeLabel"
            visible: controller.serviceOutcome.length > 0
            text: controller.serviceOutcome
            color: "#e6c57b"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "statusLabel"
            text: controller.statusText
            color: controller.connected ? "#83e1ce" : "#99abbf"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            text: controller.serialAvailable
                ? "Connect TCP or UART to read a device. Demo device uses simulated values."
                : "Connect TCP to read a device. Demo device uses simulated values."
            color: "#718297"
            font.pixelSize: 11
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
    }
    Connections {
        target: controller.values
        function onModelReset() {
            Qt.callLater(function() {
                valueTree.forceLayout()
                valueTree.expand(0)
            })
        }
    }
    Connections {
        target: controller.responses
        function onModelReset() {
            Qt.callLater(function() {
                responseTree.forceLayout()
                responseTree.expand(0)
            })
        }
    }
    Connections {
        target: controller.endpoints
        function onModelReset() {
            Qt.callLater(function() { endpointTree.expandRecursively(0, 2) })
        }
    }
    Connections {
        target: controller
        function onReadFinished(published) {
            if (published)
                Qt.callLater(function() {
                    valueTree.forceLayout()
                    valueTree.expand(0)
                })
        }
    }
}
