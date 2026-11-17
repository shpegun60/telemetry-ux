// One ordered recipe over the host provider; no client, timer or byte codec. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

Pane {
    id: panel
    required property var provider
    property int providerRevision: 0
    objectName: "batchPane"
    implicitHeight: batchLayout.implicitHeight + topPadding + bottomPadding
    // The family's surface: no card, no radius (BA-4c).
    background: Rectangle { color: Theme.surfaceWindow }
    // The panel's one primary action, as Details' (BA-4c).
    component AccentButton: Button {
        font.weight: Type.headingWeight
        palette.button: Theme.accent
        palette.buttonText: Theme.accentForeground
        palette.brightText: Theme.accentForeground
        palette.disabled.button: Theme.surfaceDisabled
        palette.disabled.buttonText: Theme.textDisabled
    }
    // A failure's 12 px glyph: a ring with "!", as Details draws it.
    component ErrorGlyph: Shape {
        id: glyph
        readonly property real s: width / 12
        implicitWidth: Math.round(Type.bodyPixels * 0.9)
        implicitHeight: implicitWidth
        preferredRendererType: Shape.CurveRenderer
        Accessible.ignored: true
        ShapePath {
            strokeColor: Theme.error
            strokeWidth: Math.max(1, glyph.s * 1.2)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: glyph.s * 6
            startY: glyph.s * 0.9
            PathArc { x: glyph.s * 6; y: glyph.s * 11.1; radiusX: glyph.s * 5.1; radiusY: glyph.s * 5.1 }
            PathArc { x: glyph.s * 6; y: glyph.s * 0.9; radiusX: glyph.s * 5.1; radiusY: glyph.s * 5.1 }
            PathMove { x: glyph.s * 6; y: glyph.s * 3.4 }
            PathLine { x: glyph.s * 6; y: glyph.s * 6.9 }
            PathMove { x: glyph.s * 6; y: glyph.s * 8.9 }
            PathLine { x: glyph.s * 6; y: glyph.s * 9.0 }
        }
    }
    // The widths of the list's fixed columns: the item number and the kind.
    FontMetrics {
        id: captionFixed
        font.family: Type.fixedFamily
        font.pointSize: Type.caption
    }
    FontMetrics {
        id: captionFont
        font.pointSize: Type.caption
    }
    readonly property real numberWidth: Math.ceil(captionFixed.advanceWidth("32"))
    readonly property real kindWidth: Math.ceil(Math.max(captionFont.advanceWidth(kindWord("read")),
                                                         captionFont.advanceWidth(kindWord("write")),
                                                         captionFont.advanceWidth(kindWord("command")),
                                                         captionFont.advanceWidth(kindWord("service"))))
    // Whether the items can be moved now: by the keys, the menu or a drag.
    readonly property bool canReorder: !provider.busy && !provider.batchPending && provider.batchCount > 1
    // An item's kind in the list's column, in the design's words.
    function kindWord(operation) {
        return operation === "read" ? qsTr("read")
             : operation === "write" ? qsTr("write")
             : operation === "command" ? qsTr("command")
             : operation === "service" ? qsTr("service") : operation
    }
    ColumnLayout {
        id: batchLayout
        anchors.fill: parent
        spacing: Type.spacing
        // The header (BA-4c): the fold with "3 of 32 items", Clear flat at
        // the left, Execute Batch the accent at the right.
        RowLayout {
            objectName: "batchHeader"
            Layout.fillWidth: true
            spacing: Type.spacing
            FoldTitle {
                id: expanded
                objectName: "batchToggle"
                text: qsTr("Batch")
                countText: qsTr("%L1 of %L2 items").arg(panel.provider.batchCount).arg(32)
                checked: false
            }
            Button {
                objectName: "clearBatchButton"
                text: qsTr("Clear")
                flat: true
                enabled: panel.provider.batchCount > 0 && !panel.provider.batchPending && !panel.provider.busy
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("Remove every item. Ctrl+Z puts them back.")
                Accessible.description: ToolTip.text
                onClicked: {
                    batchNotice.text = ""
                    panel.provider.clearBatch()
                }
            }
            Item { Layout.fillWidth: true }
            AccentButton {
                objectName: "executeBatchButton"
                text: qsTr("Execute Batch")
                enabled: panel.provider.canExecuteBatch
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("Sends every item in one request, in order. This cannot be undone.")
                Accessible.description: ToolTip.text
                onClicked: {
                    batchNotice.text = ""
                    panel.provider.executeBatch()
                }
            }
        }
        // The run, under the header.
        ProgressLine {
            objectName: "batchProgress"
            Layout.fillWidth: true
            running: panel.provider.batchPending === true
            description: panel.provider.batchStatus
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
                    // A mouse drag moves an item (BA-4c); the wheel, touch
                    // and the scroll bar scroll.
                    acceptedButtons: Qt.NoButton
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
                    // A drag in progress (BA-4c): the item dragged and the gap
                    // it would drop into, 0 to count; -1 when none.
                    property int dragFrom: -1
                    property int dragSlot: -1
                    readonly property bool dragMoves: dragFrom >= 0 && dragSlot >= 0
                                                      && dragSlot !== dragFrom && dragSlot !== dragFrom + 1
                    // Where a dragged item would land: a 2 px accent line in the gap.
                    Rectangle {
                        objectName: "batchMoveLine"
                        visible: items.dragMoves
                        z: 3
                        x: 0
                        y: Math.max(0, items.dragSlot * Type.rowHeight - 1)
                        width: items.width
                        height: 2
                        color: Theme.accent
                        Accessible.ignored: true
                    }
                    // One line per item (BA-4c): the number, the kind, the line
                    // and the outcome, Type.rowHeight tall, striped.
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
                        readonly property bool failed: model.failed === true
                        readonly property bool current: ListView.isCurrentItem
                        readonly property bool selectedLook: current && items.activeFocus
                        // The values after the path: "value=2 factor=0.1", "= 3" for a write.
                        readonly property string values: line.length === 0 ? ""
                            : operation === "write" && line.indexOf("=") < 0 ? "= " + line : line
                        // The outcome in words; nothing for a draft.
                        readonly property string outcome: historical ? qsTr("earlier connection")
                            : status === "Draft" ? ""
                            : status === "Pending" ? qsTr("Sending\u2026")
                            : status + (changed ? qsTr(", changed since") : "")
                        width: items.width
                        height: Type.rowHeight
                        opacity: items.dragFrom === index ? 0.55 : 1
                        Accessible.role: Accessible.ListItem
                        Accessible.name: [qsTr("Item %L1").arg(item.index + 1),
                                          panel.kindWord(item.operation) + " " + item.name,
                                          item.line, item.outcome].filter(part => part.length > 0).join(", ")
                        HoverHandler { id: rowHover }
                        TapHandler {
                            // A click takes the keys to the list, as the Watchlist's
                            // does: an editor elsewhere (the "+" field) commits.
                            onTapped: {
                                items.currentIndex = item.index
                                panel.provider.selectBatch(item.index)
                                items.forceActiveFocus(Qt.MouseFocusReason)
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: panel.openRowMenu(item.index, item)
                        }
                        // Dragged by the mouse to another place (touch keeps
                        // scrolling the list): one move on release.
                        DragHandler {
                            id: rowDrag
                            objectName: "batchRowDrag"
                            target: null
                            xAxis.enabled: false
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                            enabled: panel.canReorder
                            onActiveChanged: {
                                if (active) {
                                    items.dragFrom = item.index
                                    items.dragSlot = -1
                                    items.currentIndex = item.index
                                    return
                                }
                                const from = items.dragFrom, slot = items.dragSlot
                                items.dragFrom = -1
                                items.dragSlot = -1
                                if (from >= 0 && slot >= 0 && slot !== from && slot !== from + 1)
                                    panel.moveItem(from, slot > from ? slot - 1 : slot)
                            }
                            onCentroidChanged: {
                                if (!active || items.dragFrom < 0)
                                    return
                                const at = item.mapToItem(items.contentItem, centroid.position.x, centroid.position.y)
                                items.dragSlot = Math.max(0, Math.min(items.count, Math.round(at.y / Type.rowHeight)))
                            }
                            onCanceled: {
                                items.dragFrom = -1
                                items.dragSlot = -1
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: item.current ? (items.activeFocus ? Theme.stateSelected : Theme.stateSelectedInactive)
                                 : rowHover.hovered ? Theme.stateHover
                                 : item.index % 2 === 1 ? Theme.surfaceAlternate : "transparent"
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.focusRing
                            visible: item.current && items.activeFocus
                            Accessible.ignored: true
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Type.padding
                            spacing: Type.columnGap
                            Label {
                                objectName: "batchRowNumber"
                                textFormat: Text.PlainText
                                text: item.index + 1
                                font.family: Type.fixedFamily
                                font.pointSize: Type.caption
                                color: item.selectedLook ? Theme.selectedForeground : Theme.textMuted
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: panel.numberWidth
                            }
                            Label {
                                objectName: "batchRowKind"
                                textFormat: Text.PlainText
                                text: panel.kindWord(item.operation)
                                font.pointSize: Type.caption
                                color: item.selectedLook ? Theme.selectedForeground : Theme.textSecondary
                                Layout.preferredWidth: panel.kindWidth
                            }
                            // The path bright, its values quieter, in one
                            // monospace line; cut short in the middle.
                            Item {
                                id: lineBox
                                objectName: "batchRowName"
                                readonly property string text: item.name + (item.values.length > 0 ? " " + item.values : "")
                                readonly property bool truncated: pathLabel.truncated || valuesLabel.truncated
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                Label {
                                    id: pathLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: item.name
                                    font.family: Type.fixedFamily
                                    color: item.selectedLook ? Theme.selectedForeground
                                         : item.historical ? Theme.textMuted : Theme.textPrimary
                                    elide: Text.ElideMiddle
                                    width: Math.min(implicitWidth, lineBox.width
                                        - (valuesLabel.text.length > 0 ? Math.min(valuesLabel.implicitWidth + Type.tight, lineBox.width / 2) : 0))
                                }
                                Label {
                                    id: valuesLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: pathLabel.width + Type.tight
                                    width: Math.max(0, lineBox.width - x)
                                    textFormat: Text.PlainText
                                    text: item.values
                                    font.family: Type.fixedFamily
                                    color: item.selectedLook ? Theme.selectedForeground
                                         : item.historical ? Theme.textMuted : Theme.textSecondary
                                    elide: Text.ElideRight
                                }
                                HoverHandler { id: lineHover }
                                PlainToolTip {
                                    visible: lineHover.hovered && lineBox.truncated
                                    text: lineBox.text
                                    delay: 700
                                }
                            }
                            // The outcome: a failure with the glyph and in the error colour.
                            Row {
                                spacing: Type.tight
                                visible: item.outcome.length > 0
                                ErrorGlyph {
                                    objectName: "batchRowFailed"
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: item.failed && !item.historical
                                }
                                Label {
                                    objectName: "batchRowOutcome"
                                    anchors.verticalCenter: parent.verticalCenter
                                    textFormat: Text.PlainText
                                    text: item.outcome
                                    font.pointSize: Type.caption
                                    font.italic: item.changed
                                    color: item.failed && !item.historical ? Theme.error
                                         : item.selectedLook ? Theme.selectedForeground : Theme.textMuted
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                    width: Math.min(implicitWidth, Math.max(Type.bodyPixels * 6, item.width * 0.35))
                                }
                            }
                            // On the hovered or the current row: its menu and
                            // Remove. Their place is kept, so nothing moves.
                            Row {
                                Layout.preferredWidth: Type.rowHeight * 2
                                Layout.fillHeight: true
                                ToolButton {
                                    id: rowMore
                                    objectName: "batchRowMenuButton"
                                    property int batchRowIndex: item.index
                                    visible: rowHover.hovered || item.current
                                    text: "\u2026"
                                    focusPolicy: Qt.NoFocus
                                    padding: 0
                                    implicitWidth: Type.rowHeight
                                    implicitHeight: Type.rowHeight
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 700
                                    ToolTip.text: qsTr("Item %L1: move, read or write, remove (Shift+F10)").arg(item.index + 1)
                                    Accessible.name: qsTr("Menu of item %L1").arg(item.index + 1)
                                    onClicked: panel.openRowMenu(item.index, rowMore)
                                }
                                ToolButton {
                                    objectName: "removeBatchButton"
                                    property int batchRowIndex: item.index
                                    visible: rowHover.hovered || item.current
                                    text: "\u00d7"
                                    focusPolicy: Qt.NoFocus
                                    padding: 0
                                    implicitWidth: Type.rowHeight
                                    implicitHeight: Type.rowHeight
                                    enabled: !panel.provider.busy && !panel.provider.batchPending
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 700
                                    ToolTip.text: qsTr("Remove item %L1 (Delete)").arg(item.index + 1)
                                    Accessible.name: qsTr("Remove item %L1").arg(item.index + 1)
                                    onClicked: panel.provider.removeBatch(item.index)
                                }
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
                scrolls: true
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
    // An item dragged from one place to another (BA-4c): one step where the
    // provider moves to a place, else one place at a time.
    function moveItem(from, to) {
        if (from === to || from < 0 || to < 0 || provider.batchPending || provider.busy)
            return false
        let moved = false
        if (typeof provider.moveBatchTo === "function") {
            moved = provider.moveBatchTo(from, to)
        } else {
            const step = to > from ? 1 : -1
            moved = true
            for (let at = from; at !== to && moved; at += step)
                moved = provider.moveBatch(at, step)
        }
        if (!moved)
            return false
        items.currentIndex = to
        provider.selectBatch(to)
        return true
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
