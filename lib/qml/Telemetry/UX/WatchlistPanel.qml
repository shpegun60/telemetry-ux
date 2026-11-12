// The Watchlist: the watched Fields and their latest readings. One toolbar
// row - Read once, Start, the interval and a ⋯ menu - the status under it,
// then the column titles and a row per Field. The toolbar and the titles stay
// where they are; only the rows scroll. As the panel narrows, "Every" goes,
// the rows take two lines, and Read once moves into ⋯. Presentation only: the
// caller owns the provider and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Templates as T

Pane {
    id: panel
    required property var provider
    objectName: "watchlistPane"
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
    property bool expanded: false
    // A host that gives the Watchlist its own dock sets this false: the list is
    // then always open and its title, which folds it, is not shown.
    property bool collapsible: true
    // The rows show when the list is expanded, or always when it cannot fold.
    readonly property bool open: expanded || !collapsible
    // `framed: false` drops the card for a dock.
    property bool framed: true
    // The polling status as a line under the toolbar. A host whose status bar
    // already says it sets this false.
    property bool statusShown: true

    readonly property bool active: provider.watchActive === true
    readonly property int count: provider.watchCount
    readonly property bool connected: provider.connected === true
    readonly property bool canRead: connected && count > 0 && !active
    readonly property string statusText: provider.watchPending ? qsTr("Reading…")
                                       : provider.watchStatus ? provider.watchStatus : ""
    // With watch boxes in the Device tree a Field is watched by ticking it;
    // without them, by "Watch the selected Field".
    readonly property bool treeBoxes: typeof provider.watchState === "function" &&
                                      typeof provider.setWatchMany === "function"

    // A row's status in a word or two; the provider's own word is the tooltip.
    // A word not known here is shown as it is.
    function statusWord(status) {
        switch (status) {
        case "Ok": return qsTr("OK")
        case "Pending": return qsTr("Reading…")
        case "Not read": case "NotRead": return qsTr("Not read")
        case "Stale": return qsTr("Stale")
        case "NotFound": return qsTr("Not found")
        case "Unsupported": case "UnsupportedType": return qsTr("Unsupported")
        case "Unavailable": return qsTr("Unavailable")
        case "ReadError": return qsTr("Read error")
        }
        if (status.startsWith("SubmissionRefused"))
            return qsTr("Refused")
        if (/time(d)?\s*out/i.test(status))
            return qsTr("Timed out")
        return status
    }

    // What gives way first as the panel narrows: "Every", then Read once.
    // Measured from the controls, so it holds at any zoom; the toolbar's
    // controls are as wide as their text, whatever the style's minimum, so the
    // row holds the same in any host.
    readonly property real barGap: Type.spacing
    readonly property real barCore: (fold.visible ? fold.implicitWidth + barGap : 0)
                                    + start.barWidth + barGap + interval.barWidth
                                    + barGap + more.implicitWidth
    readonly property bool readOnceInBar: availableWidth >= barCore + readOnce.barWidth + barGap
    readonly property bool everyShown: availableWidth >= barCore + readOnce.barWidth + every.implicitWidth + 2 * barGap
    // One line per Field down to about 360 px; narrower rows take two lines.
    readonly property bool tableRows: watchList.width >= Type.bodyPixels * 27
    readonly property real nameColumnWidth: Math.min(Type.bodyPixels * 21,
                                                     Math.max(Type.bodyPixels * 10.5, watchList.width * 0.38))
    // As wide as the widest status word, or its title.
    TextMetrics { id: widestStatus; font.family: panel.font.family; font.pointSize: Type.caption; text: qsTr("Unsupported") }
    readonly property real statusColumnWidth: Math.ceil(widestStatus.advanceWidth) + Type.spacing
    readonly property real removeColumnWidth: Type.rowHeight
    // Rows end left of the overlay scrollbar, so × never sits under it.
    readonly property real scrollReserve: Math.max(watchScrollBar.implicitWidth, Type.spacing) + Type.tight

    // The interval list: Continuous where the provider accepts 0, then the
    // usual steps; a stored value outside them is shown as it is; last,
    // Other…, which takes any interval in ms.
    readonly property int intervalMinimum: provider.watchIntervalMinimumMs === undefined
                                           ? 1 : provider.watchIntervalMinimumMs
    readonly property int intervalMaximum: 60000
    readonly property var intervalSteps: [0, 100, 250, 500, 1000, 2000, 5000, 10000, 60000]
    readonly property var intervalChoices: {
        const value = provider.watchIntervalMs
        const steps = intervalSteps.filter(ms => ms >= intervalMinimum)
        if (steps.indexOf(value) < 0) {
            steps.push(value)
            steps.sort((a, b) => a - b)
        }
        return steps.map(ms => ({ ms: ms, text: intervalText(ms) }))
                    .concat([{ ms: -1, text: qsTr("Other…") }])
    }
    readonly property int intervalIndex: intervalChoices.findIndex(choice => choice.ms === provider.watchIntervalMs)
    function intervalText(ms) {
        if (ms === 0)
            return qsTr("Continuous")
        if (ms === 60000)
            return qsTr("1 min")
        if (ms >= 1000)
            return qsTr("%L1 s").arg(ms / 1000)
        return qsTr("%1 ms").arg(ms)
    }

    padding: Type.padding
    implicitWidth: Type.bodyPixels * 30
    background: Rectangle {
        color: panel.framed ? Theme.surface : Theme.surfaceWindow
        radius: panel.framed ? Type.radius * 2 : 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Type.tight

        RowLayout {
            id: bar
            objectName: "watchToolbar"
            Layout.fillWidth: true
            Layout.minimumHeight: Type.controlHeight
            spacing: panel.barGap
            // The title, which folds the list: a chevron, "Watchlist" and how
            // many. One state, two ways in: a host may set `expanded` and the
            // title shows it; a click sets it.
            T.AbstractButton {
                id: fold
                objectName: "watchlistExpanded"
                visible: panel.collapsible
                checkable: true
                checked: panel.expanded
                onCheckedChanged: if (panel.expanded !== checked) panel.expanded = checked
                focusPolicy: Qt.StrongFocus
                hoverEnabled: true
                leftPadding: Type.tight
                rightPadding: Type.spacing
                implicitWidth: implicitContentWidth + leftPadding + rightPadding
                implicitHeight: Type.controlHeight
                text: qsTr("Watchlist")
                contentItem: Row {
                    spacing: Type.tight
                    Chevron {
                        anchors.verticalCenter: parent.verticalCenter
                        open: fold.checked
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: fold.text
                        font.weight: Type.headingWeight
                        color: Theme.textPrimary
                    }
                    Label {
                        objectName: "watchCount"
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: Type.tight
                        text: qsTr("%L1").arg(panel.count)
                        color: Theme.textMuted
                        font.pointSize: Type.caption
                    }
                }
                background: Rectangle {
                    radius: Type.radius
                    color: fold.down ? Theme.statePressed : fold.hovered ? Theme.stateHover : "transparent"
                    border.width: fold.visualFocus ? 2 : 0
                    border.color: Theme.focusRing
                }
                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Watchlist, %L1 Fields").arg(panel.count)
                Accessible.checkable: true
                Accessible.checked: checked
            }
            Button {
                id: readOnce
                objectName: "refreshWatchButton"
                readonly property real barWidth: implicitContentWidth + leftPadding + rightPadding
                Layout.preferredWidth: barWidth
                Layout.preferredHeight: Type.controlHeight
                verticalPadding: 0
                visible: panel.readOnceInBar
                text: qsTr("Read once")
                enabled: panel.canRead
                onClicked: panel.provider.refreshWatch()
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("Read every watched Field once")
            }
            Button {
                id: start
                objectName: "startWatchButton"
                // As wide as the longer of its two words, so it does not jump.
                readonly property real barWidth: Math.max(startMetrics.advanceWidth, stopMetrics.advanceWidth)
                                                 + leftPadding + rightPadding
                TextMetrics { id: startMetrics; font: start.font; text: qsTr("Start") }
                TextMetrics { id: stopMetrics; font: start.font; text: qsTr("Stop") }
                Layout.preferredWidth: barWidth
                Layout.preferredHeight: Type.controlHeight
                verticalPadding: 0
                text: panel.active ? qsTr("Stop") : qsTr("Start")
                enabled: panel.active || (panel.connected && panel.count > 0)
                onClicked: panel.active ? panel.provider.stopWatch() : panel.provider.startWatch()
                Accessible.name: panel.active ? qsTr("Stop polling") : qsTr("Start polling")
            }
            Label {
                id: every
                objectName: "watchEvery"
                visible: panel.everyShown
                text: qsTr("Every")
                color: Theme.textMuted
                font.pointSize: Type.caption
            }
            // The interval's place: the list, or Other…'s field in its stead.
            // It says why while polling keeps it still, which a disabled list
            // under the pointer cannot.
            Item {
                id: intervalSlot
                Layout.preferredWidth: interval.barWidth
                Layout.preferredHeight: Type.controlHeight
                HoverHandler { id: intervalHover }
                ToolTip.visible: intervalHover.hovered && !interval.popup.visible
                ToolTip.delay: 700
                ToolTip.text: panel.active ? qsTr("Stop to change the interval.")
                            : panel.provider.watchIntervalMs === 0
                              ? qsTr("Read again as soon as a read finishes, at low priority: other requests go first.")
                              : intervalInput.visible ? qsTr("%L1 to %L2 ms, then Enter.").arg(panel.intervalMinimum).arg(panel.intervalMaximum)
                              : qsTr("How often to read. Fields are read one after another.")
                ComboBox {
                    id: interval
                    objectName: "watchInterval"
                    anchors.fill: parent
                    visible: !intervalInput.visible
                    model: panel.intervalChoices
                    textRole: "text"
                    implicitContentWidthPolicy: ComboBox.WidestText
                    readonly property real barWidth: implicitContentWidth + leftPadding + rightPadding
                    verticalPadding: 0
                    currentIndex: panel.intervalIndex
                    enabled: !panel.active
                    onActivated: index => {
                        const ms = panel.intervalChoices[index].ms
                        if (ms >= 0)
                            panel.provider.watchIntervalMs = ms
                        else
                            intervalInput.begin()
                    }
                    // Back to the provider's interval, whatever the list showed.
                    function resync() {
                        currentIndex = Qt.binding(() => panel.intervalIndex)
                    }
                    Accessible.name: qsTr("Polling interval")
                }
                // Other…: any interval, in milliseconds, in the list's place.
                // Enter sets it; Esc or a click elsewhere leaves it as it was.
                TextField {
                    id: intervalInput
                    objectName: "watchIntervalInput"
                    anchors.fill: parent
                    visible: false
                    topPadding: 0
                    bottomPadding: 0
                    rightPadding: unit.width + 2 * Type.spacing
                    validator: IntValidator { bottom: panel.intervalMinimum; top: panel.intervalMaximum }
                    inputMethodHints: Qt.ImhDigitsOnly
                    Accessible.name: qsTr("Polling interval in milliseconds")
                    Label {
                        id: unit
                        anchors.right: parent.right
                        anchors.rightMargin: Type.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("ms")
                        color: Theme.textMuted
                    }
                    function begin() {
                        text = String(panel.provider.watchIntervalMs)
                        visible = true
                        forceActiveFocus()
                        selectAll()
                    }
                    function finish() {
                        visible = false
                        interval.resync()
                        interval.forceActiveFocus()
                    }
                    onAccepted: {
                        panel.provider.watchIntervalMs = Number(text)
                        finish()
                    }
                    Keys.onEscapePressed: finish()
                    onActiveFocusChanged: if (!activeFocus && visible) {
                        visible = false
                        interval.resync()
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
            Basic.ToolButton {
                id: more
                objectName: "watchMoreButton"
                implicitWidth: Type.controlHeight
                implicitHeight: Type.controlHeight
                padding: 0
                focusPolicy: Qt.TabFocus
                text: qsTr("More Watchlist actions")
                display: AbstractButton.IconOnly
                contentItem: Item {
                    Row {
                        anchors.centerIn: parent
                        spacing: dot
                        readonly property int dot: Math.max(2, Math.round(Type.bodyPixels * 0.2))
                        Repeater {
                            model: 3
                            Rectangle {
                                required property int index
                                width: parent.dot
                                height: parent.dot
                                radius: parent.dot / 2
                                color: more.enabled ? Theme.textPrimary : Theme.textDisabled
                            }
                        }
                    }
                }
                background: Rectangle {
                    radius: Type.radius
                    color: more.down || moreMenu.visible ? Theme.statePressed : more.hovered ? Theme.stateHover : "transparent"
                    border.width: more.visualFocus ? 2 : 0
                    border.color: Theme.focusRing
                }
                onClicked: moreMenu.popup(more, 0, more.height)
                ToolTip.visible: (hovered || visualFocus) && !moreMenu.visible
                ToolTip.delay: 700
                ToolTip.text: text
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: text
            }
        }

        Menu {
            id: moreMenu
            objectName: "watchMoreMenu"
            readonly property bool readManySupported: panel.provider.watchReadManySupported !== false
            readonly property bool explicitIndividual: panel.provider.watchRequiresExplicitIndividual === true
            // Without ReadMany, and without a provider that wants the choice
            // made, Fields are read one by one and there is nothing to choose.
            readonly property bool choosable: readManySupported || explicitIndividual
            readonly property bool individual: !choosable || panel.provider.watchIndividual === true
            // As wide as its widest item, whatever the style's own width; the
            // plain items keep the check column, so every label lines up.
            readonly property real widestItem: {
                let widest = 0
                for (let i = 0; i < count; ++i) {
                    const item = itemAt(i)
                    if (item)
                        widest = Math.max(widest, item.implicitWidth)
                }
                return widest
            }
            width: Math.max(implicitWidth, widestItem + leftPadding + rightPadding)
            MenuItem {
                objectName: "refreshWatchItem"
                visible: !panel.readOnceInBar
                height: panel.readOnceInBar ? 0 : implicitHeight
                leftPadding: readManyItem.leftPadding
                text: qsTr("Read once")
                enabled: panel.canRead
                onTriggered: panel.provider.refreshWatch()
            }
            MenuItem {
                objectName: "addWatchItem"
                leftPadding: readManyItem.leftPadding
                text: qsTr("Watch the selected Field")
                enabled: panel.provider.canAddWatch === true && !panel.active
                onTriggered: {
                    // Through the property: setting the title's state would
                    // break its binding to `expanded`.
                    if (panel.provider.addWatch())
                        panel.expanded = true
                }
            }
            MenuSeparator {}
            MenuItem {
                id: readManyItem
                objectName: "watchModeReadMany"
                visible: moreMenu.choosable
                height: moreMenu.choosable ? implicitHeight : 0
                ButtonGroup.group: modes
                checkable: true
                checked: !moreMenu.individual
                enabled: !panel.active
                text: moreMenu.readManySupported ? qsTr("Read with ReadMany") : qsTr("Read with ReadMany (unavailable)")
                onTriggered: panel.provider.watchIndividual = false
            }
            MenuItem {
                objectName: "watchModeFieldRead"
                ButtonGroup.group: modes
                checkable: true
                checked: moreMenu.individual
                enabled: moreMenu.choosable && !panel.active
                text: qsTr("Read each Field with FieldRead")
                onTriggered: panel.provider.watchIndividual = true
            }
            MenuSeparator {}
            MenuItem {
                objectName: "clearWatchItem"
                leftPadding: readManyItem.leftPadding
                text: qsTr("Clear the Watchlist…")
                enabled: panel.count > 0 && !panel.active
                onTriggered: clearDialog.open()
            }
        }

        // The status under the toolbar, wrapped: a failure's advice is read
        // whole, however long, and the toolbar does not move as it changes.
        Label {
            objectName: "watchStatusLabel"
            visible: panel.statusShown && text.length > 0
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: panel.statusText
            color: Theme.textSecondary
            font.pointSize: Type.caption
            wrapMode: Text.Wrap
        }
        // The outcome of the last write from a row, in words.
        Label {
            objectName: "watchWriteOutcome"
            visible: panel.open && text.length > 0
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: panel.provider.watchWriteOutcome === undefined ? "" : panel.provider.watchWriteOutcome
            color: Theme.textSecondary
            font.pointSize: Type.caption
            wrapMode: Text.Wrap
        }
        // Column titles over the one-line rows; they stay while the rows scroll.
        RowLayout {
            objectName: "watchColumns"
            visible: panel.open && panel.tableRows && panel.count > 0
            Layout.fillWidth: true
            Layout.rightMargin: panel.scrollReserve
            Layout.topMargin: Type.tight
            spacing: Type.padding
            Label { objectName: "watchColumnName"; text: qsTr("Name"); color: Theme.textMuted; font.pointSize: Type.caption; Layout.leftMargin: Type.padding; Layout.preferredWidth: panel.nameColumnWidth - Type.padding }
            Label { objectName: "watchColumnValue"; text: qsTr("Value"); color: Theme.textMuted; font.pointSize: Type.caption; horizontalAlignment: Text.AlignRight; Layout.fillWidth: true }
            Label { objectName: "watchColumnStatus"; text: qsTr("Status"); color: Theme.textMuted; font.pointSize: Type.caption; Layout.preferredWidth: panel.statusColumnWidth }
            Item { Layout.preferredWidth: panel.removeColumnWidth }
        }

        // Folded, the list keeps its place at no height rather than hiding:
        // shown and given its model at once, a view makes its rows twice.
        ListView {
            id: watchList
            objectName: "watchlistView"
            Layout.fillWidth: true
            Layout.fillHeight: panel.open
            Layout.preferredHeight: panel.open ? Math.max(120, Type.rowHeight * 5) : 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.open ? panel.provider.watchlist : null
            // The keyboard: arrows move the current row, Delete removes it,
            // F2 or Enter writes a writable one.
            activeFocusOnTab: panel.open
            keyNavigationEnabled: true
            currentIndex: -1
            onActiveFocusChanged: if (activeFocus && currentIndex < 0 && count > 0) currentIndex = 0
            Keys.onPressed: event => {
                if (currentIndex < 0)
                    return
                if (event.key === Qt.Key_Delete && !panel.active) {
                    panel.provider.removeWatch(currentIndex)
                    event.accepted = true
                } else if ((event.key === Qt.Key_F2 || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                           && currentItem && currentItem.writable) {
                    currentItem.startEdit()
                    event.accepted = true
                }
            }
            ScrollBar.vertical: ScrollBar {
                id: watchScrollBar
                objectName: "watchlistScrollBar"
                policy: ScrollBar.AsNeeded
            }
            delegate: Item {
                id: watchRow
                objectName: "watchlistRow"
                required property int index
                required property string name
                required property string status
                required property string text
                // Optional roles (writable, sampleValue, sampleExact) are read
                // through the model object, so other providers need not have them.
                required property var model
                width: watchList.width
                // A writable Field is changed in its own row, as in Live
                // Expressions: double-click the value (or the pencil, or F2),
                // Enter writes, Esc or clicking elsewhere cancels.
                readonly property bool writable: model.writable === true &&
                                                 panel.provider.canWriteWatch === true
                property bool editing: false
                function startEdit() {
                    if (!writable || editing)
                        return
                    editing = true
                    valueEditor.text = model.sampleExact === true ? String(model.sampleValue) : watchRow.text
                    valueEditor.forceActiveFocus()
                    valueEditor.selectAll()
                }
                function commitEdit() {
                    if (!editing)
                        return
                    const value = valueEditor.text
                    finishEdit()
                    panel.provider.writeWatch(watchRow.index, value)
                }
                // The list is a focus scope: the editor lets go of its focus
                // first, or the list would hand it back to the hidden editor.
                function finishEdit() {
                    editing = false
                    valueEditor.focus = false
                    watchList.forceActiveFocus()
                }
                // Narrow panels give a Field two lines - its name and ×, then
                // its value and status - and wide ones one table row.
                readonly property bool compact: !panel.tableRows
                readonly property bool ok: status === "Ok"
                // Waiting is not a problem; Stale and failures are.
                readonly property bool problem: !ok && status !== "" && status !== "Pending" && status !== "Not read" && status !== "NotRead"
                readonly property bool current: ListView.isCurrentItem && watchList.activeFocus
                readonly property bool removeShown: compact || rowHover.hovered || current
                readonly property color foreground: current ? Theme.selectedForeground : Theme.textPrimary
                height: compact ? 2 * Type.rowHeight : Type.rowHeight
                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: panel.scrollReserve
                    color: watchRow.current ? Theme.stateSelected
                         : rowHover.hovered ? Theme.stateHover
                         : watchRow.index % 2 ? Theme.surfaceAlternate : "transparent"
                }
                HoverHandler { id: rowHover }
                TapHandler {
                    onTapped: {
                        watchList.currentIndex = watchRow.index
                        if (!watchRow.editing)
                            watchList.forceActiveFocus(Qt.MouseFocusReason)
                    }
                }
                // One grid for both shapes: a table row is name, value, status,
                // ×; two lines put × after the name and the status after the
                // value. Every line is a row high and nothing wraps.
                GridLayout {
                    id: rowLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: panel.scrollReserve
                    anchors.verticalCenter: parent.verticalCenter
                    columns: watchRow.compact ? 2 : 4
                    rowSpacing: 0
                    columnSpacing: watchRow.compact ? Type.spacing : Type.padding
                    Label {
                        Layout.row: 0
                        Layout.column: 0
                        Layout.leftMargin: Type.padding
                        Layout.fillWidth: watchRow.compact
                        Layout.preferredWidth: watchRow.compact ? -1 : panel.nameColumnWidth - Type.padding
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: watchRow.name
                        color: watchRow.foreground
                        elide: Text.ElideMiddle
                        ToolTip.visible: nameHover.hovered && truncated
                        ToolTip.text: watchRow.name
                        HoverHandler { id: nameHover }
                    }
                    Label {
                        objectName: "watchValueText"
                        visible: !watchRow.editing
                        Layout.row: watchRow.compact ? 1 : 0
                        Layout.column: watchRow.compact ? 0 : 1
                        Layout.leftMargin: watchRow.compact ? Type.padding : 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: 1
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: watchRow.text
                        font.family: Type.fixedFamily
                        color: watchRow.current ? Theme.selectedForeground
                             : watchRow.ok ? Theme.textPrimary : Theme.textMuted
                        horizontalAlignment: watchRow.compact ? Text.AlignLeft : Text.AlignRight
                        elide: Text.ElideRight
                        ToolTip.visible: valueHover.hovered && truncated
                        ToolTip.text: watchRow.text
                        HoverHandler { id: valueHover }
                        TapHandler {
                            enabled: watchRow.writable
                            onDoubleTapped: watchRow.startEdit()
                        }
                        ToolButton {
                            objectName: "watchEditButton"
                            text: "✎"
                            visible: watchRow.writable && (rowHover.hovered || visualFocus)
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Type.rowHeight
                            height: Type.rowHeight
                            padding: 0
                            focusPolicy: Qt.NoFocus
                            onClicked: watchRow.startEdit()
                            Accessible.name: qsTr("Write %1").arg(watchRow.name)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Write a new value (F2, or double-click the value)")
                        }
                    }
                    TextField {
                        id: valueEditor
                        objectName: "watchValueEditor"
                        visible: watchRow.editing
                        Layout.row: watchRow.compact ? 1 : 0
                        Layout.column: watchRow.compact ? 0 : 1
                        Layout.fillWidth: true
                        Layout.minimumWidth: Type.bodyPixels * 4.5
                        Layout.preferredHeight: Type.rowHeight
                        font.family: Type.fixedFamily
                        horizontalAlignment: watchRow.compact ? TextInput.AlignLeft : TextInput.AlignRight
                        topPadding: Type.tight
                        bottomPadding: Type.tight
                        // Enter is taken here: a text field passes it on after
                        // accepting, and the list would open the editor again.
                        Keys.onReturnPressed: watchRow.commitEdit()
                        Keys.onEnterPressed: watchRow.commitEdit()
                        Keys.onEscapePressed: watchRow.finishEdit()
                        // Clicking elsewhere cancels: nothing is written by accident.
                        onActiveFocusChanged: if (!activeFocus && watchRow.editing) watchRow.editing = false
                        Accessible.name: qsTr("New value for %1").arg(watchRow.name)
                    }
                    // A good reading is a quiet dot; anything else is its word,
                    // the provider's own in the tooltip.
                    Item {
                        visible: watchRow.ok
                        Layout.row: watchRow.compact ? 1 : 0
                        Layout.column: watchRow.compact ? 1 : 2
                        Layout.preferredWidth: panel.statusColumnWidth
                        Layout.preferredHeight: Type.rowHeight
                        Rectangle {
                            objectName: "watchOkDot"
                            readonly property int side: 2 * Math.round(Type.bodyPixels * 0.3)
                            width: side
                            height: side
                            radius: side / 2
                            color: Theme.success
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Label {
                        objectName: "watchRowStatus"
                        visible: !watchRow.ok
                        Layout.row: watchRow.compact ? 1 : 0
                        Layout.column: watchRow.compact ? 1 : 2
                        Layout.minimumWidth: 1
                        Layout.preferredWidth: watchRow.compact ? Math.min(panel.statusColumnWidth, watchList.width * 0.4)
                                                                : panel.statusColumnWidth
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: panel.statusWord(watchRow.status)
                        color: watchRow.current ? Theme.selectedForeground
                             : watchRow.problem ? Theme.warning : Theme.textMuted
                        font.pointSize: Type.caption
                        elide: Text.ElideRight
                        ToolTip.visible: statusRowHover.hovered && (truncated || text !== watchRow.status)
                        ToolTip.text: watchRow.status
                        HoverHandler { id: statusRowHover }
                    }
                    // × shows on the row under the pointer and on the current
                    // row; the keyboard removes with Delete.
                    Item {
                        Layout.row: 0
                        Layout.column: watchRow.compact ? 1 : 3
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredWidth: panel.removeColumnWidth
                        Layout.preferredHeight: Type.rowHeight
                        Basic.ToolButton {
                            id: remove
                            objectName: "removeWatchButton"
                            anchors.fill: parent
                            visible: watchRow.removeShown
                            focusPolicy: Qt.NoFocus
                            padding: 0
                            text: "×"
                            enabled: !panel.active
                            font.pointSize: Type.body
                            contentItem: Text {
                                text: remove.text
                                font: remove.font
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: !remove.enabled ? Theme.textDisabled
                                     : remove.hovered ? Theme.textPrimary : Theme.textSecondary
                            }
                            background: Rectangle {
                                radius: Type.radius
                                color: remove.down ? Theme.statePressed : remove.hovered ? Theme.stateHover : "transparent"
                            }
                            onClicked: panel.provider.removeWatch(watchRow.index)
                            Accessible.name: qsTr("Remove %1").arg(watchRow.name)
                            ToolTip.visible: hovered
                            ToolTip.delay: 700
                            ToolTip.text: qsTr("Remove from the Watchlist (Delete)")
                        }
                    }
                }
            }
            // A focus frame for the list itself: the rows show which is current.
            // Both it and the empty line sit on the view, not on its content.
            Rectangle {
                parent: watchList
                anchors.fill: parent
                color: "transparent"
                border.width: watchList.activeFocus && watchList.currentIndex < 0 ? 2 : 0
                border.color: Theme.focusRing
            }
            Label {
                objectName: "watchEmpty"
                parent: watchList
                anchors.centerIn: parent
                width: parent.width - 2 * Type.padding
                visible: panel.open && panel.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: Theme.textMuted
                text: !panel.connected ? qsTr("Connect to a device to watch its Fields.")
                    : panel.treeBoxes ? qsTr("Tick Fields in the Device tree to watch them.")
                    : qsTr("Select a Field in the Device tree, then choose Watch the selected Field in ⋯.")
            }
        }
    }

    // The two read modes: one is always chosen.
    ButtonGroup { id: modes; exclusive: true }

    // Clearing asks first. Cancel has the focus, so Enter keeps the list;
    // Remove all is drawn in the danger colour, not the accent.
    Dialog {
        id: clearDialog
        objectName: "clearWatchDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        // A width of its own: sized from its title and buttons, a dialog and
        // its button row would each wait for the other.
        width: Math.min(Type.bodyPixels * 30, parent ? parent.width - 2 * Type.groupSpacing : Type.bodyPixels * 30)
        modal: true
        palette.window: Theme.surface
        palette.windowText: Theme.textPrimary
        palette.base: Theme.surface
        palette.text: Theme.textPrimary
        palette.button: Theme.buttonFill
        palette.buttonText: Theme.textPrimary
        palette.highlight: Theme.stateSelected
        palette.highlightedText: Theme.selectedForeground
        title: panel.count === 1 ? qsTr("Remove the one Field from the Watchlist?")
                                 : qsTr("Remove all %L1 Fields from the Watchlist?").arg(panel.count)
        footer: DialogButtonBox {
            Button {
                objectName: "clearWatchConfirm"
                text: panel.count === 1 ? qsTr("Remove") : qsTr("Remove all")
                palette.buttonText: Theme.error
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }
            Button {
                id: clearCancel
                objectName: "clearWatchCancel"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            onDiscarded: {
                clearDialog.close()
                panel.provider.clearWatch()
            }
        }
        onOpened: clearCancel.forceActiveFocus()
    }
}
