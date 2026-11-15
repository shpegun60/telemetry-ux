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
            Button {
                objectName: "executeBatchButton"
                text: "Execute Batch"
                enabled: panel.provider.canExecuteBatch
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("Run every item on the device in order. Undo does not take back what it sends.")
                Accessible.description: ToolTip.text
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
                    onCurrentIndexChanged: if (currentIndex >= 0 && currentIndex !== panel.provider.batchSelection)
                        panel.provider.selectBatch(currentIndex)
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
                        required property var model
                        // A host's own list may lack these roles.
                        readonly property string line: typeof model.line === "string" ? model.line : ""
                        readonly property bool changed: model.changed === true
                        width: items.width
                        height: 64
                        Accessible.role: Accessible.ListItem
                        Accessible.name: qsTr("Item %1, %2 %3, %4").arg(item.index + 1).arg(item.operation).arg(item.name).arg(item.status)
                        TapHandler {
                            onTapped: {
                                items.currentIndex = item.index
                                panel.provider.selectBatch(item.index)
                            }
                        }
                        RowLayout {
                            anchors.fill: parent
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label {
                                    id: itemName
                                    objectName: "batchRowName"
                                    textFormat: Text.PlainText
                                    // The values follow the path: "control.Scale value=2 factor=0.1".
                                    text: (item.index + 1) + ". " + item.operation + " - " + item.name
                                        + (item.line.length === 0 ? "" : item.operation === "write" && item.line.indexOf("=") < 0
                                           ? " = " + item.line : " " + item.line)
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
                                Label {
                                    textFormat: Text.PlainText
                                    text: item.status + (item.historical ? " - previous session" : "")
                                        + (item.changed ? qsTr(", changed since") : "")
                                    color: Theme.textSecondary; Layout.fillWidth: true; elide: Text.ElideRight
                                }
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
                    onLeaveUp: {
                        items.forceActiveFocus()
                        if (items.count > 0)
                            items.currentIndex = items.count - 1
                    }
                }
                // What the row last did, in words: a refusal in the error
                // colour.
                Label {
                    id: batchNotice
                    objectName: "batchAddNotice"
                    visible: batchAdd.visible && text.length > 0
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: batchAdd.lastRefused ? Theme.error : Theme.textSecondary
                    font.pointSize: Type.caption
                    Layout.fillWidth: true
                    Layout.leftMargin: Type.padding + Type.indent + Type.tight
                    Layout.rightMargin: Type.padding
                    Layout.bottomMargin: Type.tight
                }
            }
            EndpointPanel {
                id: itemPane
                objectName: "batchItemPane"
                SplitView.preferredWidth: 400
                SplitView.minimumWidth: 220
                framed: false
                readonly property var item: panel.provider.batchItem
                provider: item
                infoPrefix: item && item.item > 0 ? qsTr("Item %L1 \u00b7 ").arg(item.item) : ""
                emptyResponseText: qsTr("Execute the Batch to see the response.")
                emptyText: qsTr("Choose an item to see its values here.")
                contextHelp: item && item.serviceSelected
                    ? qsTr("Request values start at zero, not at device defaults. Execute Batch calls it with the other items, in order.")
                    : item && item.commandSelected
                    ? qsTr("Arguments start at zero, not at device defaults. Execute Batch runs it with the other items, in order.")
                    : qsTr("Execute the Batch to read it.")
                // A read refuses nothing while changes are off; a write, a
                // Command and a Service say why their values stay values.
                refusalText: !item || item.item === 0 || item.historical || item.allowMutations
                    || item.operation === "read" ? ""
                    : item.operation === "write" ? qsTr("Changes are off. Allow changes to write %1.").arg(item.selection)
                    : qsTr("Changes are off. Allow changes to execute the Batch with %1.").arg(item.selection)
                // Read | Write for a Field that can be both.
                actionsComponent: item && item.switchable ? readWrite : null
                Component {
                    id: readWrite
                    RowLayout {
                        objectName: "batchReadWrite"
                        spacing: 0
                        Button {
                            objectName: "batchReadButton"
                            text: qsTr("Read")
                            checkable: true
                            checked: itemPane.item.operation === "read"
                            enabled: !itemPane.item.busy
                            onClicked: { itemPane.item.setOperation("read"); checked = Qt.binding(function() { return itemPane.item.operation === "read" }) }
                            ToolTip.visible: hovered; ToolTip.delay: 700
                            ToolTip.text: qsTr("Read %1").arg(itemPane.item.selection)
                            Accessible.name: qsTr("Read %1").arg(itemPane.item.selection)
                        }
                        Button {
                            objectName: "batchWriteButton"
                            text: qsTr("Write")
                            checkable: true
                            checked: itemPane.item.operation === "write"
                            enabled: !itemPane.item.busy && itemPane.item.allowMutations
                            onClicked: { itemPane.item.setOperation("write"); checked = Qt.binding(function() { return itemPane.item.operation === "write" }) }
                            ToolTip.visible: hovered; ToolTip.delay: 700
                            ToolTip.text: itemPane.item.allowMutations
                                ? qsTr("Write %1: starts from the last value read").arg(itemPane.item.selection)
                                : qsTr("Changes are off: allow changes to write %1.").arg(itemPane.item.selection)
                            Accessible.name: qsTr("Write %1").arg(itemPane.item.selection)
                        }
                    }
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
}
