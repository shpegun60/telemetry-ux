// One ordered recipe over the host provider; no client, timer or byte codec. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: panel
    required property var provider
    property int providerRevision: 0
    objectName: "batchPane"
    implicitHeight: batchLayout.implicitHeight + topPadding + bottomPadding
    background: Rectangle { color: "#17202b"; radius: 10 }
    ColumnLayout {
        id: batchLayout
        anchors.fill: parent
        spacing: Type.spacing
        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                id: expanded
                objectName: "batchToggle"
                text: "Batch " + panel.provider.batchCount + "/32"
                checked: false
            }
            ComboBox {
                id: operation
                objectName: "batchOperation"
                model: ["read", "write", "command", "service"]
                enabled: !panel.provider.batchPending
                Layout.preferredWidth: 130
            }
            Button {
                objectName: "addBatchButton"
                text: "Add selected draft"
                enabled: panel.providerRevision >= 0 && panel.provider.canAddBatch(operation.currentText)
                onClicked: if (panel.provider.addBatch(operation.currentText)) expanded.checked = true
            }
            Button {
                objectName: "executeBatchButton"
                text: "Execute Batch"
                enabled: panel.provider.canExecuteBatch
                onClicked: panel.provider.executeBatch()
            }
            Button {
                objectName: "clearBatchButton"
                text: "Clear"
                enabled: panel.provider.batchCount > 0 && !panel.provider.batchPending && !panel.provider.busy
                onClicked: panel.provider.clearBatch()
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            objectName: "batchOutcomeLabel"
            textFormat: Text.PlainText
            text: panel.provider.batchPending ? "Waiting for one ordered Batch reply..."
                : panel.provider.batchStatus + (panel.provider.batchSupported ? ""
                    : " - Batch is unavailable for this connection; previous outcomes are historical.")
            color: "#e6c57b"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        SplitView {
            visible: expanded.checked
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 220
            Layout.preferredHeight: 220
            ListView {
                id: items
                objectName: "batchList"
                SplitView.fillWidth: true
                SplitView.minimumWidth: 320
                model: panel.provider.batchItems
                clip: true
                delegate: Item {
                    id: item
                    objectName: "batchRow"
                    required property int index
                    required property string name
                    required property string operation
                    required property string status
                    required property bool hasValue
                    required property bool historical
                    width: items.width
                    height: 64
                    RowLayout {
                        anchors.fill: parent
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { textFormat: Text.PlainText; text: (item.index + 1) + ". " + item.operation + " - " + item.name; Layout.fillWidth: true; elide: Text.ElideMiddle }
                            Label { textFormat: Text.PlainText; text: item.status + (item.historical ? " - previous session" : ""); color: "#99abbf"; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                        Button {
                            objectName: "selectBatchButton"
                            property int batchRowIndex: item.index
                            text: item.hasValue ? "View result" : "Select"
                            enabled: !panel.provider.batchPending
                            onClicked: panel.provider.selectBatch(item.index)
                        }
                        Button {
                            objectName: "moveBatchUpButton"
                            property int batchRowIndex: item.index
                            text: "Up"
                            enabled: !panel.provider.busy && item.index > 0
                            onClicked: panel.provider.moveBatch(item.index, -1)
                        }
                        Button {
                            objectName: "moveBatchDownButton"
                            property int batchRowIndex: item.index
                            text: "Down"
                            enabled: !panel.provider.busy && item.index + 1 < panel.provider.batchCount
                            onClicked: panel.provider.moveBatch(item.index, 1)
                        }
                        Button {
                            objectName: "removeBatchButton"
                            property int batchRowIndex: item.index
                            text: "Remove"
                            enabled: !panel.provider.busy
                            onClicked: panel.provider.removeBatch(item.index)
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
            ColumnLayout {
                SplitView.preferredWidth: 400
                SplitView.minimumWidth: 220
                Label {
                    objectName: "batchResponseHeading"
                    textFormat: Text.PlainText
                    text: panel.provider.batchResponseEndpoint.length ? "Result - " + panel.provider.batchResponseEndpoint : "Select an item to inspect its result"
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TreeView {
                        id: responseTree
                        objectName: "batchResponseTree"
                        anchors.fill: parent
                        clip: true
                        model: panel.provider.batchResponses
                        columnWidthProvider: function() { return Math.max(1, responseTree.width) }
                        onWidthChanged: Qt.callLater(function() { if (responseTree) responseTree.forceLayout() })
                        delegate: TreeViewDelegate {
                            id: response
                            objectName: "batchResponseRow"
                            required property string name
                            required property string detail
                            required property string value
                            required property bool isAggregate
                            required property int childCount
                            required property int loadedCount
                            implicitWidth: responseTree.width
                            implicitHeight: Type.rowHeight
                            text: name
                            PlainToolTip { text: response.detail; visible: response.hovered && text.length > 0 }
                            contentItem: RowLayout {
                                Label { textFormat: Text.PlainText; text: response.name; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { textFormat: Text.PlainText; text: response.value; font.family: "monospace"; color: "#83e1ce" }
                                Button {
                                    text: "Load next 128"
                                    visible: response.isAggregate && response.childCount > 128
                                    enabled: response.loadedCount < response.childCount
                                    onClicked: panel.provider.batchResponses.fetchMore(responseTree.modelIndex(Qt.point(0, response.row)))
                                }
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                    }
                    Label { objectName: "batchResponseEmpty"; textFormat: Text.PlainText; anchors.centerIn: parent; visible: responseTree.rows === 0; text: "No current response value"; color: "#99abbf" }
                }
            }
        }
    }
    Connections {
        target: panel.provider
        function onStateChanged() { ++panel.providerRevision }
    }
    Connections {
        target: panel.provider.batchResponses
        function onModelReset() {
            Qt.callLater(function() { if (responseTree) { responseTree.forceLayout(); responseTree.expand(0) } })
        }
    }
}
