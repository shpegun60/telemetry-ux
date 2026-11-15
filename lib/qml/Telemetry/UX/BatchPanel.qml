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
            FoldTitle {
                id: expanded
                objectName: "batchToggle"
                text: qsTr("Batch")
                countText: qsTr("%L1/32").arg(panel.provider.batchCount)
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
                onClicked: {
                    batchNotice.text = ""
                    panel.provider.executeBatch()
                }
            }
            Button {
                objectName: "clearBatchButton"
                text: "Clear"
                enabled: panel.provider.batchCount > 0 && !panel.provider.batchPending && !panel.provider.busy
                onClicked: {
                    batchNotice.text = ""
                    panel.provider.clearBatch()
                }
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
            ColumnLayout {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 320
                    spacing: 0
                ListView {
                    id: items
                    objectName: "batchList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: panel.provider.batchItems
                    clip: true
                    // The current item, the one just added among them.
                    highlight: Rectangle { color: Theme.stateSelectedInactive; radius: Type.radius }
                    highlightMoveDuration: 0
                    highlightResizeDuration: 0
                    // Down from the last item, or Insert, goes to the "+" row.
                    Keys.onDownPressed: event => {
                        if (batchAdd.visible && items.currentIndex >= items.count - 1) {
                            batchAdd.begin()
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Insert && batchAdd.visible) {
                            batchAdd.begin()
                            event.accepted = true
                        }
                    }
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
                        Accessible.role: Accessible.ListItem
                        Accessible.name: qsTr("Item %1, %2 %3, %4").arg(item.index + 1).arg(item.operation).arg(item.name).arg(item.status)
                        TapHandler { onTapped: items.currentIndex = item.index }
                        RowLayout {
                            anchors.fill: parent
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label {
                                    id: itemName
                                    objectName: "batchRowName"
                                    textFormat: Text.PlainText
                                    text: (item.index + 1) + ". " + item.operation + " - " + item.name
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    // Cut short: the whole of it on hover.
                                    HoverHandler { id: itemNameHover }
                                    PlainToolTip {
                                        visible: itemNameHover.hovered && itemName.truncated
                                        text: itemName.text
                                        delay: 700
                                    }
                                }
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
                // Adding by name or path (BA-1), where the provider can.
                BatchAddRow {
                    id: batchAdd
                    visible: typeof panel.provider.addBatchPaths === "function"
                    provider: panel.provider
                    Layout.fillWidth: true
                    onAdded: notice => {
                        batchNotice.text = notice
                        // The new item is the current one, in view; the
                        // keys stay in the row for the next.
                        if (!batchAdd.lastRefused)
                            Qt.callLater(() => {
                                items.currentIndex = items.count - 1
                                items.positionViewAtEnd()
                            })
                    }
                    onSelected: kind => {
                        const at = operation.find(kind)
                        if (at >= 0)
                            operation.currentIndex = at
                    }
                    onLeaveUp: {
                        items.forceActiveFocus()
                        if (items.count > 0)
                            items.currentIndex = items.count - 1
                    }
                }
                // What the row last did, in words: a refusal in the error
                // colour; beside it, for a Command or a Service on the line,
                // the way to Details by pointer as Ctrl+Enter is by key.
                RowLayout {
                    visible: batchAdd.visible && (batchNotice.text.length > 0 || fillDetails.wanted)
                    Layout.fillWidth: true
                    Layout.leftMargin: Type.padding + Type.indent + Type.tight
                    Layout.rightMargin: Type.padding
                    Layout.bottomMargin: Type.tight
                    spacing: Type.spacing
                    Label {
                        id: batchNotice
                        objectName: "batchAddNotice"
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: batchAdd.lastRefused ? Theme.error : Theme.textSecondary
                        font.pointSize: Type.caption
                        Layout.fillWidth: true
                    }
                    Button {
                        id: fillDetails
                        objectName: "batchAddFillDetails"
                        // What the line names (not the operation box beside it).
                        readonly property string named: batchAdd.check.operation || ""
                        // Its own condition: the row's visibility follows it.
                        readonly property bool wanted: batchAdd.text.trim().length > 0
                                                       && (fillDetails.named === "command" || fillDetails.named === "service")
                                                       && (batchAdd.check.path || "").length > 0
                        visible: wanted
                        flat: true
                        focusPolicy: Qt.NoFocus
                        text: qsTr("Fill in Details")
                        ToolTip.text: qsTr("Fill in Details (Ctrl+Enter)")
                        ToolTip.visible: hovered
                        ToolTip.delay: 700
                        Accessible.description: qsTr("Selects %1 in the Device tree with the values typed here.").arg(batchAdd.check.path || "")
                        onClicked: batchAdd.adapter.select(batchAdd.text)
                    }
                }
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
                        // A zoom changes the row height: the rows already laid out follow it.
                        Connections {
                            target: Type
                            function onRowHeightChanged() { Qt.callLater(function() { if (responseTree) responseTree.forceLayout() }) }
                        }
                        objectName: "batchResponseTree"
                        anchors.fill: parent
                        clip: true
                        model: panel.provider.batchResponses
                        columnWidthProvider: function() { return Math.max(1, responseTree.width) }
                        onWidthChanged: Qt.callLater(function() { if (responseTree) responseTree.forceLayout() })
                        delegate: TreeViewDelegate {
                            id: response
                            indentation: Type.indent
                            indicator: TreeChevron { row: response }
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
        function onStateChanged() {
            ++panel.providerRevision
            // The Batch changed under the row: what Enter would do may too.
            if (batchAdd.editing)
                batchAdd.recheck()
        }
    }
    Connections {
        target: panel.provider.batchResponses
        function onModelReset() {
            Qt.callLater(function() { if (responseTree) { responseTree.forceLayout(); responseTree.expand(0) } })
        }
    }
}
