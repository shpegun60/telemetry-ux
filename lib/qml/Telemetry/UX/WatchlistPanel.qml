// Reusable presentation: the caller owns the provider and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Pane {
    id: panel
    required property var provider
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
    objectName: "watchlistPane"
    property bool expanded: false
    // A host that gives the Watchlist its own dock sets this false: the list is
    // then always open and the fold box is not shown.
    property bool collapsible: true
    // The rows show when the list is expanded, or always when it cannot fold.
    readonly property bool open: expanded || !collapsible
    // Wide panels show one line per Field as a table; narrow ones wrap.
    readonly property bool tableRows: watchList.width >= 500
    readonly property real nameColumnWidth: Math.min(280, Math.max(140, watchList.width * 0.38))
    readonly property real statusColumnWidth: 130
    readonly property real removeColumnWidth: 30
    // Table rows end left of the overlay scrollbar, so the remove button
    // never sits under it.
    readonly property real scrollReserve: tableRows ? Math.max(watchScrollBar.width, 10) + 2 : 0
    property real headerImplicitHeight: 0
    // Preserve either the top, a keyboard-focused control, or the user's
    // chosen relative scroll offset as the header resizes.
    property string scrollAnchor: "top"
    property real scrollOffset: 0
    property bool adjustingScroll: false
    property real seenHeaderHeight: 0
    implicitHeight: headerImplicitHeight + (open ? 120 : 0) + topPadding + bottomPadding
    background: Rectangle { color: "#17202b"; radius: 10 }

    // A dock can be much narrower or shorter than the Configurator. Wrap the
    // control groups and scroll vertically instead of clipping actions. The
    // view remains purely presentational: visibility never changes polling.
    contentItem: ScrollView {
        id: watchScroll
        objectName: "watchlistScroll"
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: ScrollBar {
            id: watchScrollBar
            objectName: "watchlistScrollBar"
            policy: ScrollBar.AsNeeded
            active: true
            onPressedChanged: if (pressed) {
                panel.scrollAnchor = "free"
                panel.scrollOffset = Math.max(0, watchList.contentY - watchList.originY)
            }
            parent: watchScroll
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }
        contentItem: ListView {
            id: watchList
            objectName: "watchlistView"
            clip: true
            onContentYChanged: {
                if (!panel.adjustingScroll && headerItem &&
                        Math.abs(headerItem.height - panel.seenHeaderHeight) < 0.5) {
                    panel.scrollOffset = Math.max(0, contentY - originY)
                    panel.scrollAnchor = panel.scrollOffset <= 1 ? "top" : "free"
                }
            }
            onMovementStarted: {
                panel.scrollAnchor = "free"
                panel.scrollOffset = Math.max(0, contentY - originY)
            }
            onMovementEnded: {
                panel.scrollAnchor = atYBeginning ? "top" : "free"
                panel.scrollOffset = Math.max(0, contentY - originY)
            }
            model: panel.open ? panel.provider.watchlist : null
            header: ColumnLayout {
                id: watchLayout
                width: watchList.width
                onImplicitHeightChanged: panel.headerImplicitHeight = implicitHeight
                onHeightChanged: {
                    // The inline header sits before row zero. Keep its top
                    // anchored across layout changes only until user scrolls.
                    if (!watchList.moving) {
                        if (panel.scrollAnchor === "focus") {
                            panel.revealFocusedControl()
                        } else {
                            panel.adjustingScroll = true
                            watchList.contentY = -height + (panel.scrollAnchor === "free" ? panel.scrollOffset : 0)
                            panel.adjustingScroll = false
                        }
                    }
                    panel.seenHeaderHeight = height
                }
                Component.onCompleted: panel.headerImplicitHeight = implicitHeight
                spacing: 6
                Flow {
                    id: actions
                    objectName: "watchActions"
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    spacing: 6
                    CheckBox {
                        id: watchPanelExpanded
                        objectName: "watchlistExpanded"
                        visible: panel.collapsible
                        text: "Watchlist " + panel.provider.watchCount
                        // One state, two ways in: a host may set `expanded`
                        // (e.g. a program that opens the list by default) and
                        // the box shows it; ticking the box sets it.
                        checked: panel.expanded
                        onCheckedChanged: if (panel.expanded !== checked) panel.expanded = checked
                    }
                    Button {
                        id: addWatchControl
                        objectName: "addWatchButton"
                        text: "Add selected Field"
                        enabled: panel.provider.canAddWatch && !panel.provider.watchActive
                        onClicked: {
                            const reason = addWatchControl.focusReason
                            if (panel.provider.addWatch()) {
                                // Through the property: setting the box here
                                // would break its binding to `expanded`.
                                panel.expanded = true
                                addWatchControl.forceActiveFocus(reason)
                            }
                        }
                    }
                    Button {
                        objectName: "refreshWatchButton"
                        text: "Read once"
                        enabled: panel.provider.connected && panel.provider.watchCount > 0 && !panel.provider.watchActive
                        onClicked: panel.provider.refreshWatch()
                    }
                    Button {
                        objectName: "startWatchButton"
                        text: panel.provider.watchActive ? "Stop" : "Start polling"
                        enabled: panel.provider.watchActive || (panel.provider.connected && panel.provider.watchCount > 0)
                        onClicked: panel.provider.watchActive ? panel.provider.stopWatch() : panel.provider.startWatch()
                    }
                }
                Label {
                    id: watchStatusLabel
                    objectName: "watchStatusLabel"
                    textFormat: Text.PlainText
                    text: panel.provider.watchPending ? "Reading…" : panel.provider.watchStatus
                    color: "#99abbf"
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Flow {
                    objectName: "watchSettings"
                    visible: panel.open
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    spacing: 6
                    ComboBox {
                        objectName: "watchMode"
                        width: Math.min(210, watchLayout.width)
                        readonly property bool readManySupported: panel.provider.watchReadManySupported !== false
                        readonly property bool explicitIndividual: panel.provider.watchRequiresExplicitIndividual === true
                        model: readManySupported ? ["ReadMany", "Individual FieldRead"]
                            : explicitIndividual ? ["ReadMany (unavailable)", "Individual FieldRead"] : ["Individual FieldRead"]
                        currentIndex: (readManySupported || explicitIndividual) && panel.provider.watchIndividual ? 1 : 0
                        enabled: !panel.provider.watchActive && (readManySupported || explicitIndividual)
                        onActivated: panel.provider.watchIndividual = (!readManySupported && !explicitIndividual) || currentIndex === 1
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Interval (ms)"
                        color: "#99abbf"
                        height: watchFrequency.height
                        verticalAlignment: Text.AlignVCenter
                    }
                    SpinBox {
                        id: watchFrequency
                        objectName: "watchFrequency"
                        // 0 (continuous) only where the provider accepts it.
                        from: panel.provider.watchIntervalMinimumMs === undefined
                              ? 1 : panel.provider.watchIntervalMinimumMs
                        to: 60000
                        stepSize: 1
                        editable: true
                        value: panel.provider.watchIntervalMs
                        enabled: !panel.provider.watchActive
                        onValueModified: panel.provider.watchIntervalMs = value
                    }
                    Button {
                        objectName: "clearWatchButton"
                        text: "Clear"
                        enabled: panel.provider.watchCount > 0 && !panel.provider.watchActive
                        onClicked: panel.provider.clearWatch()
                    }
                }
                // The outcome of the last write from a row, in words.
                Label {
                    objectName: "watchWriteOutcome"
                    visible: panel.open && text.length > 0
                    textFormat: Text.PlainText
                    text: panel.provider.watchWriteOutcome === undefined ? "" : panel.provider.watchWriteOutcome
                    color: "#f2d28a"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                Label {
                    objectName: "watchIntervalHint"
                    visible: panel.open
                    textFormat: Text.PlainText
                    text: panel.provider.watchIntervalMs === 0
                          ? "0 ms: read continuously, at low priority - other requests go first."
                          : "Nominal interval. Fields are read sequentially."
                    color: "#99abbf"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                // Column titles over the one-line table rows.
                RowLayout {
                    objectName: "watchColumns"
                    visible: panel.open && panel.tableRows && panel.provider.watchCount > 0
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    Label { objectName: "watchColumnName"; text: "Name"; color: "#8597ab"; font.pixelSize: 12; Layout.leftMargin: 8; Layout.preferredWidth: panel.nameColumnWidth - 8 }
                    Label { objectName: "watchColumnValue"; text: "Value"; color: "#8597ab"; font.pixelSize: 12; horizontalAlignment: Text.AlignRight; Layout.fillWidth: true }
                    Label { objectName: "watchColumnStatus"; text: "Status"; color: "#8597ab"; font.pixelSize: 12; Layout.preferredWidth: panel.statusColumnWidth }
                    Item { Layout.preferredWidth: panel.removeColumnWidth; Layout.rightMargin: panel.scrollReserve }
                }
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
                // Expressions: double-click the value (or the pencil), Enter
                // writes, Esc or clicking elsewhere cancels.
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
                    editing = false
                    panel.provider.writeWatch(watchRow.index, value)
                }
                // Narrow panels wrap name, value and status over two lines; wide
                // ones show one 28 px table row per Field.
                readonly property bool compact: !panel.tableRows
                readonly property bool ok: status === "Ok"
                // Waiting is not a problem; Stale and failures are.
                readonly property bool problem: !ok && status !== "" && status !== "Pending" && status !== "Not read" && status !== "NotRead"
                height: compact ? rowLayout.implicitHeight + 8 : 28
                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: panel.scrollReserve
                    visible: !watchRow.compact
                    radius: 3
                    color: rowHover.hovered ? "#22303f" : (watchRow.index % 2 ? "#151d27" : "transparent")
                }
                HoverHandler { id: rowHover }
                ColumnLayout {
                    id: rowLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.rightMargin: watchRow.compact ? 0 : panel.scrollReserve
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Label {
                        visible: watchRow.compact
                        textFormat: Text.PlainText
                        text: watchRow.name
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: watchRow.compact ? 5 : 8
                        Label {
                            visible: !watchRow.compact
                            textFormat: Text.PlainText
                            text: watchRow.name
                            Layout.leftMargin: 8
                            Layout.preferredWidth: panel.nameColumnWidth - 8
                            elide: Text.ElideMiddle
                            ToolTip.visible: nameHover.hovered && truncated
                            ToolTip.text: watchRow.name
                            HoverHandler { id: nameHover }
                        }
                        Label {
                            objectName: "watchValueText"
                            visible: !watchRow.editing
                            textFormat: Text.PlainText
                            text: watchRow.text
                            font.family: "monospace"
                            color: watchRow.ok ? "#83e1ce" : "#8597ab"
                            horizontalAlignment: watchRow.compact ? Text.AlignLeft : Text.AlignRight
                            Layout.fillWidth: true
                            Layout.minimumWidth: 1
                            wrapMode: watchRow.compact ? Text.WrapAnywhere : Text.NoWrap
                            elide: watchRow.compact ? Text.ElideNone : Text.ElideRight
                            Layout.fillHeight: watchRow.compact
                            Layout.minimumHeight: watchRow.compact ? paintedHeight : 0
                            ToolTip.visible: valueHover.hovered && truncated
                            ToolTip.text: watchRow.text
                            HoverHandler { id: valueHover }
                            TapHandler {
                                enabled: watchRow.writable
                                onDoubleTapped: watchRow.startEdit()
                            }
                            ToolButton {
                                objectName: "watchEditButton"
                                text: "\u270E"
                                visible: watchRow.writable && (rowHover.hovered || visualFocus)
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 24
                                height: 24
                                padding: 0
                                onClicked: watchRow.startEdit()
                                Accessible.name: "Write " + watchRow.name
                                ToolTip.visible: hovered
                                ToolTip.text: "Write a new value (or double-click the value)"
                            }
                        }
                        TextField {
                            id: valueEditor
                            objectName: "watchValueEditor"
                            visible: watchRow.editing
                            font.family: "monospace"
                            horizontalAlignment: watchRow.compact ? TextInput.AlignLeft : TextInput.AlignRight
                            Layout.fillWidth: true
                            Layout.minimumWidth: 60
                            Layout.preferredHeight: watchRow.compact ? implicitHeight : 24
                            topPadding: 2
                            bottomPadding: 2
                            onAccepted: watchRow.commitEdit()
                            Keys.onEscapePressed: watchRow.editing = false
                            // Clicking elsewhere cancels: nothing is written by accident.
                            onActiveFocusChanged: if (!activeFocus && watchRow.editing) watchRow.editing = false
                            Accessible.name: "New value for " + watchRow.name
                        }
                        // In a table row a good reading is a quiet dot; only a
                        // problem is spelled out (the full text is its tooltip).
                        Item {
                            visible: !watchRow.compact && watchRow.ok
                            Layout.preferredWidth: panel.statusColumnWidth
                            implicitHeight: 16
                            Rectangle {
                                objectName: "watchOkDot"
                                width: 8
                                height: 8
                                radius: 4
                                color: "#5fd0a0"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Label {
                            objectName: "watchRowStatus"
                            visible: watchRow.compact || !watchRow.ok
                            textFormat: Text.PlainText
                            text: watchRow.status
                            color: watchRow.problem && !watchRow.compact ? "#f2b96a" : "#99abbf"
                            Layout.minimumWidth: 1
                            Layout.preferredWidth: watchRow.compact ? Math.min(100, watchList.width / 4) : panel.statusColumnWidth
                            wrapMode: watchRow.compact ? Text.WrapAnywhere : Text.NoWrap
                            elide: watchRow.compact ? Text.ElideNone : Text.ElideRight
                            Layout.fillHeight: watchRow.compact
                            Layout.minimumHeight: watchRow.compact ? paintedHeight : 0
                            ToolTip.visible: statusHover.hovered && truncated
                            ToolTip.text: watchRow.status
                            HoverHandler { id: statusHover }
                        }
                        Button {
                            objectName: "removeWatchButton"
                            property int watchRowIndex: watchRow.index
                            text: watchRow.compact ? "Remove" : "×"
                            flat: !watchRow.compact
                            Layout.preferredWidth: watchRow.compact ? implicitWidth : panel.removeColumnWidth
                            Layout.preferredHeight: watchRow.compact ? implicitHeight : 24
                            enabled: !panel.provider.watchActive
                            onClicked: panel.provider.removeWatch(watchRow.index)
                            Accessible.name: "Remove " + watchRow.name
                            ToolTip.visible: hovered && !watchRow.compact
                            ToolTip.text: "Remove from the Watchlist"
                            // A flat button shows no frame; keyboard focus needs one.
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                radius: 3
                                border.width: 1
                                border.color: "#83e1ce"
                                visible: !watchRow.compact && parent.visualFocus
                            }
                        }
                    }
                }
            }
            footer: Label {
                textFormat: Text.PlainText
                width: watchList.width
                height: visible ? 80 : 0
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                visible: panel.open && panel.provider.watchCount === 0
                text: "Select a scalar Field and add it to the watchlist."
                color: "#718297"
            }
        }
    }
    function revealFocusedControl() {
        const window = panel.Window.window
        const focused = window ? window.activeFocusItem : null
        if (!focused || focused === watchList.currentItem)
            return false
        let ancestor = focused
        while (ancestor && ancestor !== watchList.contentItem)
            ancestor = ancestor.parent
        if (!ancestor)
            return false
        const position = focused.mapToItem(watchList.contentItem, 0, 0)
        let offset = watchList.contentY
        if (position.y < offset)
            offset = position.y
        else if (position.y + focused.height > offset + watchList.height)
            offset = position.y + focused.height - watchList.height
        const minimum = watchList.originY
        const maximum = Math.max(minimum, minimum + watchList.contentHeight - watchList.height)
        panel.adjustingScroll = true
        watchList.contentY = Math.max(minimum, Math.min(offset, maximum))
        panel.adjustingScroll = false
        return true
    }
    Connections {
        target: panel.Window.window
        function onActiveFocusItemChanged() {
            const focused = panel.Window.window.activeFocusItem
            if (panel.revealFocusedControl() && focused.objectName !== "addWatchButton" &&
                    focused.objectName !== "watchlistExpanded") {
                panel.scrollAnchor = "focus"
            }
        }
    }
}
