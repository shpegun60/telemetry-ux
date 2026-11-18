// One ordered recipe over the host provider; no client, timer or byte codec.
// The header with its status line, then the list of items beside (720 px and
// wider) or above the selected item's pane, which is Details' (EndpointPanel)
// word for word (Details layout spec §8). MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

Pane {
    id: panel
    required property var provider
    // No fading and no flashes, here and in the item's pane.
    property bool reducedMotion: false
    // The folds of every item's values, by item (uid), for the panel's life.
    FoldMemory { id: batchFolds }
    property int providerRevision: 0
    objectName: "batchPane"
    // No horizontal padding: the list's stripes and fills span it; text
    // stands on L and R (Type.padding), the header's as Details'.
    padding: 0
    implicitHeight: batchLayout.implicitHeight + 2 * Type.groupSpacing
    // The family's surface: no card, no radius (BA-4c); the family's colours
    // for its own controls, as Details sets them (Clear reads as a control).
    background: Rectangle { color: Theme.surfaceWindow }
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
    // The list's column labels: caption, Regular, textSecondary.
    component ColumnLabel: Label {
        textFormat: Text.PlainText
        elide: Text.ElideRight
        font.pointSize: Type.caption
        color: Theme.textSecondary
        verticalAlignment: Text.AlignVCenter
        Accessible.ignored: true
    }
    // The widths of the list's fixed columns: the item number, the kind
    // (the widest kind word or its label) and the outcome (the widest shown,
    // growing only, from its label's width, at most 35 % of the list).
    // The labels' own font (the panel's family), at the caption size. The
    // widths read the font, so they follow a zoom (a method call alone would
    // not make them).
    FontMetrics {
        id: captionFixed
        font.family: Type.fixedFamily
        font.pointSize: Type.caption
    }
    FontMetrics {
        id: captionFont
        font.family: panel.font.family
        font.pointSize: Type.caption
    }
    readonly property real numberWidth: {
        void captionFixed.font
        return Math.ceil(captionFixed.advanceWidth("32"))
    }
    readonly property real kindWidth: {
        void captionFont.font
        return Math.ceil(Math.max(captionFont.advanceWidth(kindWord("read")), captionFont.advanceWidth(kindWord("write")),
                                  captionFont.advanceWidth(kindWord("command")), captionFont.advanceWidth(kindWord("service")),
                                  captionFont.advanceWidth(qsTr("Operation"))))
    }
    property real outcomeSeen: 0
    function noteOutcome(width) { if (width > outcomeSeen) outcomeSeen = width }
    // A zoom measures the outcomes again.
    Connections { target: Type; function onCaptionChanged() { panel.outcomeSeen = 0 } }
    readonly property real outcomeWidth: {
        void captionFont.font
        return Math.ceil(Math.min(Math.max(captionFont.advanceWidth(qsTr("Outcome")), outcomeSeen),
                                  Math.max(Type.bodyPixels * 4, items.width * 0.35)))
    }
    // The x of each column in the list: | 6 | # | 12 | Operation | 12 | Item | 12 | Outcome | … × | 6 |.
    readonly property real kindX: Type.padding + numberWidth + Type.columnGap
    readonly property real itemX: kindX + kindWidth + Type.columnGap
    // Whether the items can be moved now: by the keys, the menu or a drag.
    readonly property bool canReorder: !provider.busy && !provider.batchPending && provider.batchCount > 1
    // An item's kind in the list's column, in the design's words.
    function kindWord(operation) {
        return operation === "read" ? qsTr("read")
             : operation === "write" ? qsTr("write")
             : operation === "command" ? qsTr("command")
             : operation === "service" ? qsTr("service") : operation
    }
    // The status line, never empty: the provider's words, or what to do.
    readonly property string statusText: {
        void providerRevision
        if (typeof provider.batchStatus === "string" && provider.batchStatus.length > 0)
            return provider.batchStatus
        return provider.batchCount > 0 ? qsTr("Not executed yet")
            : qsTr("Add Fields, Commands and Services; Execute Batch sends them in one request, in order.")
    }
    // A run ended: the status line's time flashes once, as Details' does.
    readonly property bool pending: provider.batchPending === true
    property bool wasPending: false
    property int statusFlashes: 0
    onPendingChanged: {
        if (pending) {
            wasPending = true
        } else if (wasPending) {
            wasPending = false
            if (!reducedMotion && provider.batchAttention !== true && batchStatusTime.text.length > 0 && batchStatusTime.fits) {
                ++statusFlashes
                batchStatusFlash.restart()
            }
        }
    }
    // An item's path in the list: its group muted, its name bright.
    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
    function pathMarkup(path, muted) {
        const cut = path.lastIndexOf(".")
        if (cut < 0)
            return escaped(path)
        return "<font color=\"" + muted + "\">" + escaped(path.substring(0, cut + 1)) + "</font>" + escaped(path.substring(cut + 1))
    }
    ColumnLayout {
        id: batchLayout
        anchors.fill: parent
        anchors.topMargin: Type.groupSpacing
        anchors.bottomMargin: Type.groupSpacing
        spacing: 0
        // The header (BA-4c): the fold with "3 of 32 items", Clear flat
        // beside it, Execute Batch the accent ending on R.
        RowLayout {
            objectName: "batchHeader"
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            Layout.preferredHeight: Type.controlHeight
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
                Layout.preferredHeight: Type.controlHeight
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
            // While the Batch runs it says so, and keeps its width.
            AccentButton {
                id: executeBatch
                objectName: "executeBatchButton"
                text: panel.pending ? qsTr("Sending…") : qsTr("Execute Batch")
                enabled: panel.provider.canExecuteBatch && !panel.pending
                Layout.preferredHeight: Type.controlHeight
                Layout.minimumWidth: Math.ceil(Math.max(executeWords.advanceWidth, sendingWords.advanceWidth))
                                     + leftPadding + rightPadding
                TextMetrics { id: executeWords; font: executeBatch.font; text: qsTr("Execute Batch") }
                TextMetrics { id: sendingWords; font: executeBatch.font; text: qsTr("Sending…") }
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
        // The status line (BA-4c, Details layout §8): what the Batch is doing
        // or did, in words, always shown; a failure with the glyph and its
        // words in the primary text colour, never by colour alone.
        RowLayout {
            objectName: "batchStatus"
            Layout.fillWidth: true
            Layout.topMargin: Type.tight
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.spacing
            ErrorGlyph {
                objectName: "batchStatusGlyph"
                visible: panel.provider.batchAttention === true
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Math.max(0, (batchStatusMetrics.height - height) / 2)
            }
            Label {
                id: batchStatusLabel
                objectName: "batchOutcomeLabel"
                textFormat: Text.PlainText
                text: panel.statusText
                color: panel.provider.batchAttention === true ? Theme.textPrimary : Theme.textSecondary
                font.pointSize: Type.caption
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                Layout.fillWidth: true
                Accessible.name: text
                FontMetrics { id: batchStatusMetrics; font: batchStatusLabel.font }
                // The run's time, flashed once in the accent when it ends.
                Label {
                    id: batchStatusTime
                    objectName: "batchStatusTime"
                    readonly property var found: /\b\d{1,2}:\d{2}:\d{2}\b/.exec(batchStatusLabel.text)
                    readonly property bool fits: batchStatusLabel.lineCount === 1 && !batchStatusLabel.truncated
                    text: found ? found[0] : ""
                    x: found ? Math.round(batchStatusPrefix.advanceWidth) : 0
                    height: batchStatusMetrics.height
                    textFormat: Text.PlainText
                    font: batchStatusLabel.font
                    color: Theme.accent
                    opacity: 0
                    visible: text.length > 0 && fits
                    Accessible.ignored: true
                    TextMetrics {
                        id: batchStatusPrefix
                        font: batchStatusLabel.font
                        text: batchStatusTime.found ? batchStatusLabel.text.substring(0, batchStatusTime.found.index) : ""
                    }
                }
                OpacityAnimator {
                    id: batchStatusFlash
                    target: batchStatusTime
                    from: 1
                    to: 0
                    duration: 600
                    easing.type: Easing.InQuad
                }
            }
        }
        // The list beside the item's pane from 720 px; narrower, the list
        // above (its rows up to eight, then it scrolls) and the pane below.
        SplitView {
            id: batchSplit
            objectName: "batchSplit"
            visible: expanded.checked
            orientation: width >= 720 ? Qt.Horizontal : Qt.Vertical
            readonly property bool sideBySide: orientation === Qt.Horizontal
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Type.groupSpacing
            Layout.minimumHeight: sideBySide ? 220 : 320
            Layout.preferredHeight: sideBySide ? 220 : 420
            // A 1 px rule between them, dragged from a few pixels either side.
            handle: Rectangle {
                objectName: "batchSplitHandle"
                implicitWidth: 1
                implicitHeight: 1
                color: SplitHandle.pressed || SplitHandle.hovered ? Theme.outline : Theme.outlineVariant
                containmentMask: Item {
                    x: batchSplit.sideBySide ? -3 : 0
                    y: batchSplit.sideBySide ? 0 : -3
                    width: batchSplit.sideBySide ? 7 : batchSplit.width
                    height: batchSplit.sideBySide ? batchSplit.height : 7
                }
            }
            Item {
                id: listColumn
                objectName: "batchListColumn"
                SplitView.preferredWidth: Math.max(320, Math.min(520, Math.floor(batchSplit.width / 2)))
                SplitView.minimumWidth: batchSplit.sideBySide ? 320 : 0
                SplitView.fillWidth: false
                // Narrow: the header, up to eight rows, the "+" row and its
                // notice; then the list scrolls.
                SplitView.preferredHeight: listHeader.height + Math.min(Math.max(items.count, 1), 8) * Type.rowHeight
                                           + batchAdd.height + (batchNotice.visible ? batchNotice.height + Type.tight : 0)
                SplitView.minimumHeight: batchSplit.sideBySide ? 0 : Type.rowHeight * 4
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    // The list's header row (family A9): "#" over the numbers,
                    // "Operation", "Item", "Outcome" over the outcomes, the rule.
                    Item {
                        id: listHeader
                        objectName: "batchListHeader"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Type.rowHeight
                        Accessible.role: Accessible.ColumnHeader
                        Accessible.name: qsTr("#, Operation, Item, Outcome")
                        ColumnLabel {
                            objectName: "batchListNumberLabel"
                            x: Type.padding
                            width: panel.numberWidth
                            height: parent.height
                            text: qsTr("#")
                            horizontalAlignment: Text.AlignRight
                        }
                        ColumnLabel {
                            objectName: "batchListOperationLabel"
                            x: panel.kindX
                            width: panel.kindWidth
                            height: parent.height
                            text: qsTr("Operation")
                        }
                        ColumnLabel {
                            objectName: "batchListItemLabel"
                            x: panel.itemX
                            width: Math.max(0, outcomeLabel.x - Type.columnGap - x)
                            height: parent.height
                            text: qsTr("Item")
                        }
                        ColumnLabel {
                            id: outcomeLabel
                            objectName: "batchListOutcomeLabel"
                            x: parent.width - Type.padding - Type.rowHeight * 2 - Type.columnGap - width
                            width: panel.outcomeWidth
                            height: parent.height
                            text: qsTr("Outcome")
                            horizontalAlignment: Text.AlignRight
                        }
                        Rectangle {
                            objectName: "batchListRule"
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: Theme.outlineVariant
                        }
                    }
                    ListView {
                        id: items
                        objectName: "batchList"
                        Layout.fillWidth: true
                        // As tall as its rows up to the room left, so the "+"
                        // row stands directly under the last item.
                        readonly property real room: Math.max(Type.rowHeight, listColumn.height - listHeader.height - batchAdd.height
                                                              - (batchNotice.visible ? batchNotice.height + Type.tight : 0))
                        Layout.preferredHeight: Math.min(contentHeight, room)
                        model: panel.provider.batchItems
                        clip: true
                        activeFocusOnTab: true
                        boundsBehavior: Flickable.StopAtBounds
                        // A mouse drag moves an item (BA-4c); the wheel, touch
                        // and the scroll bar scroll.
                        acceptedButtons: Qt.NoButton
                        // The current item is the one in the pane; the row draws
                        // its own square fill (§7), so there is no highlight.
                        highlightFollowsCurrentItem: false
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
                        // One line per item (family rows): the number, the kind,
                        // the path with its values, the outcome; 22 px, striped.
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
                            readonly property bool focused: current && items.activeFocus
                            // The values after the path: "value=2 factor=0.1", "= 3" for a write.
                            readonly property string values: line.length === 0 ? ""
                                : operation === "write" && line.indexOf("=") < 0 ? "= " + line : line
                            // The outcome in words: the provider's, else made from the status.
                            readonly property string outcome: typeof model.outcome === "string" ? model.outcome
                                : historical ? qsTr("earlier connection")
                                : status === "Draft" ? "–"
                                : status === "Pending" ? qsTr("Sending…")
                                : status + (changed ? qsTr(", changed since") : "")
                            readonly property bool notRun: outcome === "–"
                            width: items.width
                            height: Type.rowHeight
                            opacity: items.dragFrom === index ? 0.55 : 1
                            Accessible.role: Accessible.ListItem
                            Accessible.name: [qsTr("Item %L1").arg(item.index + 1),
                                              panel.kindWord(item.operation) + " " + item.name,
                                              item.line, item.notRun ? qsTr("not run yet") : item.outcome]
                                             .filter(part => part.length > 0).join(", ")
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
                            // The fill: stripes, hover, and the current item's
                            // selection - active while the list has the keys.
                            Rectangle {
                                objectName: "batchRowFill"
                                anchors.fill: parent
                                color: item.current ? (items.activeFocus ? Theme.stateSelected : Theme.stateSelectedInactive)
                                     : rowHover.hovered ? Theme.stateHover
                                     : item.index % 2 === 1 ? Theme.surfaceAlternate : "transparent"
                            }
                            Rectangle {
                                objectName: "batchRowFocusRing"
                                anchors.fill: parent
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.focusRing
                                visible: item.focused
                                Accessible.ignored: true
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Type.padding
                                anchors.rightMargin: Type.padding
                                spacing: Type.columnGap
                                Label {
                                    objectName: "batchRowNumber"
                                    textFormat: Text.PlainText
                                    text: item.index + 1
                                    font.family: Type.fixedFamily
                                    font.pointSize: Type.caption
                                    color: item.current ? Theme.selectedForeground : Theme.textMuted
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: panel.numberWidth
                                }
                                Label {
                                    objectName: "batchRowKind"
                                    textFormat: Text.PlainText
                                    text: panel.kindWord(item.operation)
                                    font.pointSize: Type.caption
                                    color: item.current ? Theme.selectedForeground : Theme.textSecondary
                                    Layout.preferredWidth: panel.kindWidth
                                }
                                // The path in the application font, its group muted
                                // and its name bright; then its values, fixed and
                                // quieter. The values elide first (at most half
                                // the cell), the path in the middle only after.
                                Item {
                                    id: lineBox
                                    objectName: "batchRowName"
                                    readonly property string text: item.name + (item.values.length > 0 ? " " + item.values : "")
                                    readonly property bool truncated: pathLabel.truncated || valuesLabel.truncated
                                    readonly property real valuesRoom: valuesLabel.text.length === 0 ? 0
                                        : Math.min(valuesLabel.implicitWidth, lineBox.width / 2)
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    Label {
                                        id: pathLabel
                                        objectName: "batchRowPath"
                                        anchors.verticalCenter: parent.verticalCenter
                                        textFormat: item.current || item.historical ? Text.PlainText : Text.StyledText
                                        text: item.current || item.historical ? item.name : panel.pathMarkup(item.name, Theme.textMuted)
                                        color: item.current ? Theme.selectedForeground
                                             : item.historical ? Theme.textMuted : Theme.textPrimary
                                        elide: Text.ElideMiddle
                                        width: Math.min(implicitWidth, Math.max(0, lineBox.width
                                            - (lineBox.valuesRoom > 0 ? lineBox.valuesRoom + Type.groupSpacing : 0)))
                                    }
                                    Label {
                                        id: valuesLabel
                                        objectName: "batchRowValues"
                                        anchors.verticalCenter: parent.verticalCenter
                                        x: pathLabel.width + Type.groupSpacing
                                        width: Math.max(0, lineBox.width - x)
                                        textFormat: Text.PlainText
                                        text: item.values
                                        font.family: Type.fixedFamily
                                        color: item.current ? Theme.selectedForeground
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
                                // The outcome, right-aligned in its column: "–"
                                // before a run, a failure with the glyph and in the
                                // error colour, changed since italic and muted.
                                Item {
                                    objectName: "batchRowOutcomeCell"
                                    Layout.preferredWidth: panel.outcomeWidth
                                    Layout.fillHeight: true
                                    Row {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Type.tight
                                        ErrorGlyph {
                                            objectName: "batchRowFailed"
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: item.failed && !item.historical
                                        }
                                        Label {
                                            id: outcomeText
                                            objectName: "batchRowOutcome"
                                            anchors.verticalCenter: parent.verticalCenter
                                            textFormat: Text.PlainText
                                            text: item.outcome
                                            font.pointSize: Type.caption
                                            font.italic: item.changed && !item.notRun
                                            color: item.failed && !item.historical ? Theme.error
                                                 : item.current ? Theme.selectedForeground
                                                 : item.notRun || item.changed || item.historical ? Theme.textMuted
                                                 : Theme.textSecondary
                                            horizontalAlignment: Text.AlignRight
                                            elide: Text.ElideRight
                                            width: Math.min(implicitWidth, panel.outcomeWidth
                                                            - (item.failed && !item.historical ? Math.round(Type.bodyPixels * 0.9) + Type.tight : 0))
                                            onImplicitWidthChanged: panel.noteOutcome(implicitWidth
                                                + (item.failed && !item.historical ? Math.round(Type.bodyPixels * 0.9) + Type.tight : 0))
                                            Component.onCompleted: panel.noteOutcome(implicitWidth
                                                + (item.failed && !item.historical ? Math.round(Type.bodyPixels * 0.9) + Type.tight : 0))
                                        }
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
                                        text: "…"
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
                                        text: "×"
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
                    }
                    // Adding by name or path (BA-1), where the provider can,
                    // directly under the last item: "+" in the number column,
                    // the words under the Operation column.
                    BatchAddRow {
                        id: batchAdd
                        visible: typeof panel.provider.addBatchPaths === "function"
                        provider: panel.provider
                        Layout.fillWidth: true
                        plusX: Type.padding
                        plusWidth: panel.numberWidth
                        plusAlignment: Text.AlignRight
                        textX: panel.kindX
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
                        Layout.topMargin: Type.tight
                        Layout.leftMargin: panel.kindX
                        Layout.rightMargin: Type.padding
                    }
                    // The room under the list stays empty; nothing is pinned
                    // to the bottom.
                    Item { Layout.fillWidth: true; Layout.fillHeight: true }
                }
                // Paths dropped from the Device tree or the Watchlist join
                // the Batch as the "+" row adds them, at their initial values
                // (BA-4c). Over the whole list column, so the pane keeps its size.
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
                        count = typeof panel.provider.batchItemCount === "function"
                                ? panel.provider.batchItemCount(paths) : paths.length
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
            // The selected item, as Details shows an endpoint (§1-§7), with
            // "Item 3 · " before line 2, the Batch's status words and no
            // progress line of its own: the Batch's run is one.
            EndpointPanel {
                id: itemPane
                objectName: "batchItemPane"
                SplitView.fillWidth: batchSplit.sideBySide
                SplitView.fillHeight: !batchSplit.sideBySide
                SplitView.minimumWidth: batchSplit.sideBySide ? 220 : 0
                SplitView.minimumHeight: batchSplit.sideBySide ? 0 : Type.rowHeight * 6
                progressShown: false
                reducedMotion: panel.reducedMotion
                // What was opened in each item's values stays open for as long as
                // this panel exists: moving, running, removing and undoing an
                // item, or collapsing the Batch in its dock, forget nothing.
                foldMemory: batchFolds
                foldScope: items.currentItem && items.currentItem.model && items.currentItem.model.uid !== undefined
                           ? "item:" + items.currentItem.model.uid : ""
                readonly property var item: panel.provider.batchItem
                provider: item
                infoPrefix: item && item.item > 0 ? qsTr("Item %L1 · ").arg(item.item) : ""
                emptyText: qsTr("Choose an item to see its values here.")
                contextHelp: item && item.serviceSelected
                    ? qsTr("Request values start at zero, not at device defaults. Execute Batch calls it with the other items, in order.")
                    : item && item.commandSelected
                    ? qsTr("Arguments start at zero, not at device defaults. Execute Batch runs it with the other items, in order.")
                    : qsTr("Execute Batch reads or writes it with the other items, in order.")
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
                            Layout.preferredHeight: Type.controlHeight
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
                            Layout.preferredHeight: Type.controlHeight
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
    // The run under way: one line over the top padding, no height of its own.
    ProgressLine {
        objectName: "batchProgress"
        x: 0
        y: 0
        z: 2
        width: panel.width
        running: panel.provider.batchPending === true
        reducedMotion: panel.reducedMotion
        description: panel.statusText
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
