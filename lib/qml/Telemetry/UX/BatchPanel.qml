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
        // The status line (BA-4c): what the Batch is doing or did, in words;
        // muted unless something went wrong.
        Label {
            objectName: "batchOutcomeLabel"
            textFormat: Text.PlainText
            // The provider says it all, items of an earlier connection
            // included; a run that failed keeps its words after a disconnect.
            text: panel.provider.batchStatus
            color: panel.provider.batchAttention === true ? Theme.warning : Theme.textSecondary
            font.pointSize: Type.caption
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        // The list beside the item's pane; in a narrow dock the pane goes
        // under the list, so neither is cut off at the window's edge.
        SplitView {
            id: batchSplit
            objectName: "batchSplit"
            visible: expanded.checked
            orientation: width >= 640 ? Qt.Horizontal : Qt.Vertical
            readonly property bool sideBySide: orientation === Qt.Horizontal
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: sideBySide ? 220 : 320
            Layout.preferredHeight: sideBySide ? 220 : 420
            ColumnLayout {
                    SplitView.fillWidth: batchSplit.sideBySide
                    SplitView.fillHeight: !batchSplit.sideBySide
                    SplitView.minimumWidth: batchSplit.sideBySide ? 320 : 0
                    SplitView.minimumHeight: batchSplit.sideBySide ? 0 : Type.rowHeight * 4
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
                    Keys.onUpPressed: event => {
                        if (event.modifiers & Qt.AltModifier) {
                            panel.moveCurrent(-1)
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                    Keys.onDeletePressed: event => {
                        if (items.currentIndex >= 0 && !panel.provider.batchPending)
                            panel.provider.removeBatch(items.currentIndex)
                        event.accepted = true
                    }
                    Keys.onDownPressed: event => {
                        if (event.modifiers & Qt.AltModifier) {
                            panel.moveCurrent(1)
                            event.accepted = true
                        } else if (batchAdd.visible && items.currentIndex >= items.count - 1) {
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
                        } else if ((event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) || event.key === Qt.Key_Menu) {
                            if (items.currentIndex >= 0)
                                panel.openRowMenu(items.currentIndex, items.currentItem)
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
                        HoverHandler { id: rowHover }
                        TapHandler {
                            onTapped: {
                                items.currentIndex = item.index
                                panel.provider.selectBatch(item.index)
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: panel.openRowMenu(item.index, item)
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
                            // On the hovered or the current row: its menu and Remove.
                            ToolButton {
                                id: rowMore
                                objectName: "batchRowMenuButton"
                                property int batchRowIndex: item.index
                                visible: rowHover.hovered || item.ListView.isCurrentItem
                                text: "\u2026"
                                focusPolicy: Qt.NoFocus
                                implicitWidth: Type.controlHeight
                                ToolTip.visible: hovered
                                ToolTip.delay: 700
                                ToolTip.text: qsTr("Item %L1: move, read or write, remove (Shift+F10)").arg(item.index + 1)
                                Accessible.name: qsTr("Menu of item %L1").arg(item.index + 1)
                                onClicked: panel.openRowMenu(item.index, rowMore)
                            }
                            ToolButton {
                                objectName: "removeBatchButton"
                                property int batchRowIndex: item.index
                                visible: rowHover.hovered || item.ListView.isCurrentItem
                                text: "×"
                                focusPolicy: Qt.NoFocus
                                implicitWidth: Type.controlHeight
                                enabled: !panel.provider.busy && !panel.provider.batchPending
                                ToolTip.visible: hovered
                                ToolTip.delay: 700
                                ToolTip.text: qsTr("Remove item %L1 (Delete)").arg(item.index + 1)
                                Accessible.name: qsTr("Remove item %L1").arg(item.index + 1)
                                onClicked: panel.provider.removeBatch(item.index)
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    // Paths dropped from the Device tree or the Watchlist join
                    // the Batch as the "+" row adds them, at their initial values
                    // (BA-4c). Over the list itself, so the pane keeps its size.
                    DropArea {
                        id: batchDrop
                        objectName: "batchDrop"
                        anchors.fill: parent
                        keys: ["application/x-telemetry-paths"]
                        property int count: 0
                        function pathsOf(event) {
                            let payload = null
                            try { payload = JSON.parse(event.getDataAsString("application/x-telemetry-paths")) } catch (e) {}
                            if (!payload || payload.from === "batch" || !Array.isArray(payload.paths))
                                return []
                            return payload.paths.filter((path, at) => typeof path === "string" && path.length > 0
                                                                     && payload.paths.indexOf(path) === at)
                        }
                        onEntered: drag => {
                            const paths = pathsOf(drag)
                            if (paths.length === 0 || !batchAdd.visible || !batchAdd.enabled) {
                                drag.accepted = false
                                return
                            }
                            count = paths.length
                        }
                        onExited: count = 0
                        onDropped: drop => {
                            const paths = pathsOf(drop)
                            count = 0
                            if (panel.dropPaths(paths))
                                drop.acceptProposedAction()
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: batchDrop.containsDrag
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.focusRing
                            Label {
                                objectName: "batchDropText"
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: Type.padding
                                text: batchDrop.count === 1 ? qsTr("Drop to add 1 item to the Batch")
                                                            : qsTr("Drop to add %L1 items to the Batch").arg(batchDrop.count)
                                color: Theme.textPrimary
                                padding: Type.tight
                                background: Rectangle { color: Theme.surface; radius: Type.radius }
                            }
                        }
                    }
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
                SplitView.minimumWidth: batchSplit.sideBySide ? 220 : 0
                SplitView.preferredHeight: Type.rowHeight * 13
                SplitView.minimumHeight: batchSplit.sideBySide ? 0 : Type.rowHeight * 6
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
                // Read | Write for a Field that can be both; no Read, Execute
                // or Call of Details': Execute Batch runs the item.
                actionsComponent: item && item.switchable ? readWrite : noActions
                Component {
                    id: noActions
                    Item {}
                }
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
    // Paths dropped on the list, added as the "+" row adds them; said on its
    // notice line.
    function dropPaths(paths) {
        if (paths.length === 0 || typeof provider.addBatchPaths !== "function")
            return false
        const result = batchAdd.adapter.add(paths)
        if (!result)
            return false
        batchNotice.text = result.notice
        if (result.clear)
            Qt.callLater(() => {
                expanded.checked = true
                items.currentIndex = items.count - 1
                items.positionViewAtEnd()
            })
        return result.clear === true
    }
    // The current item one place up or down (Alt+Up, Alt+Down, the menu).
    function moveCurrent(delta) {
        const at = items.currentIndex
        if (at < 0 || provider.batchPending || !provider.moveBatch(at, delta))
            return false
        items.currentIndex = at + delta
        provider.selectBatch(at + delta)
        return true
    }
    // The row's menu, on the row it was opened for, which becomes the current one.
    function openRowMenu(row, at) {
        if (row < 0)
            return
        items.currentIndex = row
        provider.selectBatch(row)
        rowMenu.row = row
        rowMenu.popup(at || items)
    }
    Menu {
        id: rowMenu
        objectName: "batchRowMenu"
        property int row: -1
        readonly property var detail: panel.provider.batchItem
        readonly property bool idle: !panel.provider.busy && !panel.provider.batchPending
        MenuItem {
            objectName: "batchMenuMoveUp"
            text: qsTr("Move up (Alt+Up)")
            enabled: rowMenu.idle && rowMenu.row > 0
            onTriggered: panel.moveCurrent(-1)
        }
        MenuItem {
            objectName: "batchMenuMoveDown"
            text: qsTr("Move down (Alt+Down)")
            enabled: rowMenu.idle && rowMenu.row >= 0 && rowMenu.row + 1 < panel.provider.batchCount
            onTriggered: panel.moveCurrent(1)
        }
        MenuSeparator {}
        // Read | Write, as the switch in the item's pane: each offered when
        // it changes the item.
        MenuItem {
            objectName: "batchMenuRead"
            text: qsTr("Make it a read")
            enabled: !!rowMenu.detail && rowMenu.detail.switchable === true && !rowMenu.detail.busy
                     && rowMenu.detail.operation === "write"
            onTriggered: rowMenu.detail.setOperation("read")
        }
        MenuItem {
            objectName: "batchMenuWrite"
            text: qsTr("Make it a write")
            enabled: !!rowMenu.detail && rowMenu.detail.switchable === true && !rowMenu.detail.busy
                     && rowMenu.detail.allowMutations === true && rowMenu.detail.operation === "read"
            onTriggered: rowMenu.detail.setOperation("write")
        }
        MenuSeparator {}
        MenuItem {
            objectName: "batchMenuRemove"
            text: qsTr("Remove (Delete)")
            enabled: rowMenu.idle && rowMenu.row >= 0
            onTriggered: panel.provider.removeBatch(rowMenu.row)
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
