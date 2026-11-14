// The Watchlist: the watched values and their latest readings. One toolbar
// row - Read once, Start, the interval, List | Tree and a ⋯ menu - the status
// under it, then the column titles and the rows: one per value with its whole
// path, or a tree grouped by path, as Live Expressions shows them. The toolbar
// and the titles stay where they are; only the rows scroll. As the panel
// narrows, "Every" goes, then Read once and List | Tree move into ⋯.
// Presentation only: the caller owns the provider and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Templates as T

Pane {
    id: panel
    required property var provider
    // Optional host workflow (for example, prepare a project recording before
    // starting). Without handlers the existing provider is called directly.
    property var startHandler: null
    property var stopHandler: null
    property bool operationActive: false
    // Why the row asked to write can't be (a value in a Field larger than one
    // write); shown in the outcome line until another row is chosen.
    property string writeRefusalText: ""
    function requestStart(polling) {
        if (typeof startHandler === "function") startHandler(polling)
        else if (polling) provider.startWatch()
        else provider.refreshWatch()
    }
    function requestStop() {
        if (typeof stopHandler === "function") stopHandler()
        else provider.stopWatch()
    }
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
    palette.disabled.buttonText: Theme.textDisabled
    palette.disabled.windowText: Theme.textDisabled
    palette.disabled.text: Theme.textDisabled
    property bool expanded: false
    // A host that gives the Watchlist its own dock sets this false: the list is
    // then always open and its title, which folds it, is not shown.
    property bool collapsible: true
    // The rows show when the list is expanded, or always when it cannot fold.
    readonly property bool open: expanded || !collapsible
    // `framed: false` drops the card for a dock.
    property bool framed: true
    // The polling status as a line under the toolbar. A host whose status bar
    // already says it sets this false; advice it would cut short still shows.
    property bool statusShown: true
    // The device cannot read several Fields at once and the Watchlist still
    // asks it to: the status is advice, read whole, in the warning colour.
    readonly property bool readManyAdvice: provider.watchReadManySupported === false
                                           && provider.watchRequiresExplicitIndividual === true
                                           && provider.watchIndividual !== true

    readonly property bool active: provider.watchActive === true || operationActive
    // Rows are added, removed and cleared while polling when the provider
    // takes changes between two reads; the interval and modes still wait.
    readonly property bool changeLocked: provider.watchActive === true && provider.watchChangesWhilePolling !== true
    readonly property int count: provider.watchCount
    readonly property bool connected: provider.connected === true
    readonly property bool canRead: connected && count > 0 && !active
    readonly property string statusText: provider.watchPending ? qsTr("Reading…")
                                       : provider.watchStatus ? provider.watchStatus : ""
    // With watch boxes in the Device tree a Field is watched by ticking it;
    // without them, by "Watch the selected Field".
    readonly property bool treeBoxes: typeof provider.watchState === "function" &&
                                      typeof provider.setWatchMany === "function"
    // The "+" row, where a provider resolves typed paths (WL-4b).
    readonly property bool addAvailable: typeof provider.addWatchPaths === "function" &&
                                         typeof provider.watchPathCandidates === "function" &&
                                         typeof provider.checkWatchPath === "function"

    // The rows as shown, from the provider: one per value with its whole
    // path, or grouped by path - group, Field, member - folding as the Device
    // tree does (a WatchRows). The view's only: polling, writes and recording
    // are the same in both. A provider without it shows its watchlist as a
    // list. The provider keeps the rows, so a fold outlives a closed list.
    readonly property var rows: provider.watchRows ?? null
    readonly property bool treeAvailable: rows !== null
    readonly property bool treeView: rows !== null && rows.tree === true
    // How the numbers are shown (WL-3): the Watchlist's form, and a row's
    // own. The view's only; a write takes any form whatever is shown.
    readonly property bool formatAvailable: typeof provider.watchFormat === "string"
    readonly property string format: formatAvailable ? provider.watchFormat : "original"
    readonly property var formats: [
        { name: "original", text: qsTr("Original"), tip: qsTr("Original: as the type means it (1.5, High, true)") },
        { name: "dec", text: qsTr("Dec"), tip: qsTr("Dec: integers and codes in decimal (2, 1)") },
        { name: "hex", text: qsTr("Hex"), tip: qsTr("Hex: the value's bits, floats included (0x3FC00000)") }
    ]
    function formatWord(name) {
        const found = formats.find(item => item.name === name)
        return found ? found.text : name
    }
    function setFormat(name) {
        if (formatAvailable)
            provider.watchFormat = name
    }
    // The Value column grows only, until a form is chosen: hex is wider.
    onFormatChanged: forgetMeasured()
    // Gives what row shows - a value, or every value under a node - a format
    // of its own, or "" to follow the Watchlist's.
    function formatRows(row, name) {
        if (typeof provider.setWatchRowFormat !== "function" || row < 0)
            return
        const sources = rows ? rows.sourceRows(row) : [row]
        if (sources.length > 0 && provider.setWatchRowFormat(sources, name))
            forgetMeasured()
    }
    // The row a menu was opened for.
    property int menuRow: -1
    function openRowMenu(row, item, x, y) {
        menuRow = row
        rowMenu.popup(item, x, y)
    }
    // Switches the view; the current value stays current, its parents opened.
    function showTree(tree) {
        if (!rows || tree === treeView)
            return
        const current = watchList.currentIndex
        const sources = current >= 0 ? rows.sourceRows(current) : []
        rows.tree = tree
        const row = sources.length > 0 ? rows.reveal(sources[0]) : -1
        watchList.currentIndex = row
        if (row >= 0)
            watchList.positionViewAtIndex(row, ListView.Contain)
    }
    // Stops watching what row shows: a value, or every value under a node, as
    // one change where the provider takes several, said with Undo.
    property string noticeText: ""
    // Whether the notice is a removal that Undo can take back.
    property bool noticeUndo: false
    function removeAt(row) {
        const sources = rows ? rows.sourceRows(row) : [row]
        if (sources.length === 0 || changeLocked)
            return
        const item = watchList.itemAtIndex(row)
        const label = item ? item.name : ""
        if (typeof provider.removeWatchRows !== "function") {
            sources.slice().sort((a, b) => b - a).forEach(source => provider.removeWatch(source))
            return
        }
        const result = provider.removeWatchRows(sources)
        if (!result || result.status !== "ok" || result.changed === 0)
            return
        noticeText = sources.length === 1 || label.length === 0
            ? (label.length > 0 ? qsTr("Removed %1").arg(label) : qsTr("Removed %L1").arg(result.changed))
            : qsTr("Removed %L1 values of %2").arg(result.changed).arg(label)
        noticeUndo = true
    }
    function undoRemoval() {
        if (typeof provider.undoWatch !== "function" || provider.canUndoWatch !== true)
            return false
        const result = provider.undoWatch()
        const done = result && result.status === "ok"
        noticeText = done ? qsTr("Undone") : ""
        noticeUndo = false
        return done
    }
    // Copies what row shows: "path = value" lines (Ctrl+C), the paths
    // alone (Alt+Shift+C), or the value alone.
    function copyRows(row, what) {
        if (row < 0)
            return false
        const item = watchList.itemAtIndex(row)
        let text = ""
        if (what === "value")
            text = item && item.isValue ? item.shownText : ""
        else if (rows)
            text = rows.lines(row, what === "paths")
        else if (item)
            text = what === "paths" || item.shownText.length === 0 ? item.name : item.name + " = " + item.shownText
        if (text.length === 0)
            return false
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.text = ""
        const lines = text.split("\n").length
        noticeText = what === "value" ? qsTr("Copied the value of %1").arg(item.name)
                   : lines === 1 ? qsTr("Copied %1").arg(what === "paths" ? text : item ? item.name : text)
                   : what === "paths" ? qsTr("Copied %L1 paths").arg(lines)
                   : qsTr("Copied %L1 values").arg(lines)
        noticeUndo = false
        return true
    }

    // A row's status in a word or two; the provider's own word is the tooltip.
    // A word not known here is shown as it is.
    function statusWord(status) {
        switch (status) {
        case "Ok": return qsTr("OK")
        case "Pending": return qsTr("Reading…")
        // A row added while polling waits for the next read; that is not a problem.
        case "Not read": case "NotRead": return panel.provider.watchActive === true ? qsTr("Next read") : qsTr("Not read")
        case "Stale": return qsTr("Stale")
        case "NotFound": return qsTr("Not found")
        case "Unsupported": case "UnsupportedType": return qsTr("Unsupported")
        case "Unavailable": return qsTr("Unavailable")
        case "ReadError": return qsTr("Read error")
        case "MalformedReply": return qsTr("Bad reply")
        }
        if (status.startsWith("SubmissionRefused"))
            return qsTr("Refused")
        if (/time(d)?\s*out/i.test(status))
            return qsTr("Timed out")
        return status
    }

    // What gives way first as the panel narrows: "Every", then Read once,
    // then List | Tree. Measured from the controls, so it holds at any zoom;
    // the toolbar's controls are as wide as their text, whatever the style's
    // minimum, so the row holds the same in any host.
    readonly property real barGap: Type.spacing
    readonly property real barBase: (fold.visible ? fold.implicitWidth + barGap : 0)
                                    + start.barWidth + barGap + interval.barWidth
                                    + 2 * barGap + more.implicitWidth
    readonly property real formatButtonWidth: formatAvailable ? formatButton.implicitWidth + 2 * barGap : 0
    readonly property real formatSegmentsWidth: formatAvailable ? formatSegments.implicitWidth + 2 * barGap : 0
    readonly property bool viewInBar: treeAvailable
                                      && availableWidth >= barBase + formatButtonWidth + viewSegments.implicitWidth + barGap
    readonly property real barCore: barBase + (viewInBar ? viewSegments.implicitWidth + barGap : 0) + formatButtonWidth
    readonly property bool readOnceInBar: (viewInBar || !treeAvailable)
                                          && availableWidth >= barCore + readOnce.barWidth + barGap
    readonly property bool formatInBar: formatAvailable && readOnceInBar
        && availableWidth >= barCore - formatButtonWidth + formatSegmentsWidth + readOnce.barWidth + barGap
    readonly property bool everyShown: readOnceInBar && (formatInBar || !formatAvailable)
        && availableWidth >= barCore - formatButtonWidth + formatSegmentsWidth + readOnce.barWidth
                             + every.implicitWidth + 2 * barGap
    // The row family's columns (family A1-A3): Name fills; Value, Type and
    // Status are as wide as the widest text they have shown, growing only
    // until the list is cleared or the zoom changes, so a reading never makes
    // them jump; rw is two letters. As the room for names shrinks, rw hides
    // below 140 px, Type narrows to 40 px below 120 and hides below 96, then
    // Status becomes a glyph whose word is in its tooltip. Name, Value and ×
    // never hide, and a row is one line except while it is edited.
    readonly property real columnGap: Math.round(Type.bodyPixels * 0.9)
    readonly property real removeColumnWidth: Type.rowHeight
    readonly property real listWidth: Math.max(0, watchList.width - scrollReserve)
    property real valueSeen: 0
    property real typeSeen: 0
    property real statusSeen: 0
    property bool accessSeen: false
    property real valuePending: 0
    property real typePending: 0
    property real statusPending: 0
    property bool accessPending: false
    function noteNumber(width) {
        if (width > numberSeen)
            numberSeen = width
    }
    function noteMeasured(value, type, status, access) {
        if (value > valuePending || type > typePending || status > statusPending || (access && !accessPending)) {
            valuePending = Math.max(valuePending, value)
            typePending = Math.max(typePending, type)
            statusPending = Math.max(statusPending, status)
            accessPending = accessPending || access
            applyMeasured.restart()
        }
    }
    // Forgets the widths (a cleared list, a new zoom) and asks every row to
    // measure itself again: a type's text never changes, so its row would
    // not report it a second time by itself.
    property int measureEpoch: 0
    function forgetMeasured() {
        valuePending = typePending = statusPending = 0
        accessPending = false
        valueSeen = typeSeen = statusSeen = numberSeen = 0
        accessSeen = false
        ++measureEpoch
    }
    // Rows report while they are made; the widths change once, after.
    Timer {
        id: applyMeasured
        interval: 0
        onTriggered: {
            panel.valueSeen = Math.max(panel.valueSeen, panel.valuePending)
            panel.typeSeen = Math.max(panel.typeSeen, panel.typePending)
            panel.statusSeen = Math.max(panel.statusSeen, panel.statusPending)
            panel.accessSeen = panel.accessSeen || panel.accessPending
        }
    }
    onCountChanged: if (count === 0) forgetMeasured()
    Connections {
        target: Type
        function onBodyPixelsChanged() { panel.forgetMeasured() }
    }
    // Numbers never elide: the column is as wide as the widest one, up to
    // more than half of a narrow list.
    // Room for the pencil at the value's left while rows can be written, so
    // it never sits on the digits.
    readonly property real pencilRoom: provider.canWriteWatch === true ? Type.rowHeight : 0
    property real numberSeen: 0
    readonly property real valueColumn: count === 0 ? 0
        : Math.max(Math.round(Type.bodyPixels * 4.2),
                   Math.min(Math.ceil(numberSeen) + pencilRoom, listWidth * 0.55),
                   Math.min(Math.ceil(valueSeen) + pencilRoom, Type.bodyPixels * 16.5, listWidth * 0.4))
    readonly property real typeFull: typeSeen > 0 ? Math.max(Type.bodyPixels * 3, Math.min(Math.ceil(typeSeen), Type.bodyPixels * 9.6)) : 0
    readonly property real accessFull: accessSeen ? Math.round(Type.bodyPixels * 1.05) : 0
    TextMetrics { id: statusTitleMetrics; font.family: panel.font.family; font.pointSize: Type.caption; text: qsTr("Status") }
    readonly property real statusFull: statusSeen > 0 ? Math.ceil(Math.max(statusSeen, statusTitleMetrics.advanceWidth)) : 0
    readonly property real glyphWidth: Math.round(Type.bodyPixels * 1.05)
    function nameRoom(type, access, status) {
        return listWidth - Type.padding - valueColumn - removeColumnWidth - 2 * columnGap
            - (treeView ? 3 * Type.indent : 0)
            - (type > 0 ? type + columnGap : 0) - (access > 0 ? access + columnGap : 0)
            - (status > 0 ? status + columnGap : 0)
    }
    readonly property real accessColumn: accessFull > 0 && nameRoom(typeFull, accessFull, statusFull) >= Type.bodyPixels * 10.5
                                         ? accessFull : 0
    readonly property real typeColumn: typeFull <= 0 ? 0
        : nameRoom(typeFull, 0, statusFull) >= Type.bodyPixels * 9 ? typeFull
        : nameRoom(Type.bodyPixels * 3, 0, statusFull) >= Type.bodyPixels * 7.2 ? Type.bodyPixels * 3 : 0
    readonly property bool statusGlyph: statusFull > 0 && nameRoom(typeColumn, accessColumn, statusFull) < Type.bodyPixels * 7.2
    readonly property real statusColumn: statusFull <= 0 ? 0 : statusGlyph ? glyphWidth : statusFull
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
            // The title, which folds the list. One state, two ways in: a host
            // may set `expanded` and the title shows it; a click sets it.
            FoldTitle {
                id: fold
                objectName: "watchlistExpanded"
                visible: panel.collapsible
                text: qsTr("Watchlist")
                countText: qsTr("%L1").arg(panel.count)
                checked: panel.expanded
                onCheckedChanged: if (panel.expanded !== checked) panel.expanded = checked
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
                onClicked: panel.requestStart(false)
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
                onClicked: panel.active ? panel.requestStop() : panel.requestStart(true)
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
                        // The panel is a focus scope: let go, or it would give
                        // the keys back to this hidden field.
                        focus = false
                        interval.resync()
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
            // List | Tree, as FileViewer's Values | Hex: checkable, the chosen
            // one filled; Left and Right move between them.
            Row {
                id: viewSegments
                objectName: "watchViewSegments"
                visible: panel.viewInBar
                Layout.preferredHeight: Type.controlHeight
                spacing: 0
                ButtonGroup { id: viewModes }
                Repeater {
                    id: viewButtons
                    model: [
                        { tree: false, text: qsTr("List"), name: qsTr("List view"),
                          tip: qsTr("List: one row per path") },
                        { tree: true, text: qsTr("Tree"), name: qsTr("Tree view"),
                          tip: qsTr("Tree: grouped by path, as in the Device tree") }
                    ]
                    delegate: Button {
                        id: segment
                        required property var modelData
                        objectName: modelData.tree ? "watchViewTree" : "watchViewList"
                        // As wide as its word, whatever the style's minimum.
                        width: implicitContentWidth + leftPadding + rightPadding
                        height: Type.controlHeight
                        verticalPadding: 0
                        font.pointSize: Type.caption
                        text: modelData.text
                        checkable: true
                        checked: panel.treeView === modelData.tree
                        flat: !checked
                        ButtonGroup.group: viewModes
                        onClicked: panel.showTree(modelData.tree)
                        Keys.onLeftPressed: {
                            panel.showTree(false)
                            viewButtons.itemAt(0).forceActiveFocus(Qt.TabFocusReason)
                        }
                        Keys.onRightPressed: {
                            panel.showTree(true)
                            viewButtons.itemAt(1).forceActiveFocus(Qt.TabFocusReason)
                        }
                        Accessible.name: modelData.name
                        Accessible.description: modelData.tip
                        ToolTip.visible: hovered
                        ToolTip.delay: 700
                        ToolTip.text: modelData.tip
                    }
                }
            }
            // Original | Dec | Hex, as List | Tree.
            Row {
                id: formatSegments
                objectName: "watchFormatSegments"
                visible: panel.formatInBar
                // A gap from List | Tree: two choices, not one of five.
                Layout.leftMargin: panel.barGap
                Layout.preferredHeight: Type.controlHeight
                spacing: 0
                ButtonGroup { id: formatModes }
                Repeater {
                    id: formatButtons
                    model: panel.formats
                    delegate: Button {
                        required property var modelData
                        required property int index
                        objectName: "watchFormat_" + modelData.name
                        width: implicitContentWidth + leftPadding + rightPadding
                        height: Type.controlHeight
                        verticalPadding: 0
                        font.pointSize: Type.caption
                        text: modelData.text
                        checkable: true
                        checked: panel.format === modelData.name
                        flat: !checked
                        ButtonGroup.group: formatModes
                        onClicked: panel.setFormat(modelData.name)
                        Keys.onLeftPressed: if (index > 0) {
                            panel.setFormat(panel.formats[index - 1].name)
                            formatButtons.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason)
                        }
                        Keys.onRightPressed: if (index < panel.formats.length - 1) {
                            panel.setFormat(panel.formats[index + 1].name)
                            formatButtons.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason)
                        }
                        Accessible.name: qsTr("%1 format").arg(modelData.text)
                        Accessible.description: modelData.tip
                        ToolTip.visible: hovered
                        ToolTip.delay: 700
                        ToolTip.text: modelData.tip
                    }
                }
            }
            // Too narrow for three: one button showing the form, "Hex ▾", as
            // wide as its widest word.
            Button {
                id: formatButton
                objectName: "watchFormatButton"
                visible: panel.formatAvailable && !panel.formatInBar
                Layout.leftMargin: panel.barGap
                TextMetrics { id: widestFormat; font: formatButton.font; text: qsTr("Original") + " ▾" }
                implicitWidth: Math.ceil(widestFormat.advanceWidth) + leftPadding + rightPadding
                Layout.preferredHeight: Type.controlHeight
                verticalPadding: 0
                flat: true
                font.pointSize: Type.caption
                text: panel.formatWord(panel.format) + " ▾"
                onClicked: formatMenu.popup(formatButton, 0, formatButton.height)
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: qsTr("Number format: %1").arg(panel.formatWord(panel.format))
                ToolTip.visible: hovered && !formatMenu.visible
                ToolTip.delay: 700
                ToolTip.text: qsTr("How the Watchlist shows its numbers")
                Menu {
                    id: formatMenu
                    objectName: "watchFormatMenu"
                    ButtonGroup { id: formatItems; exclusive: true }
                    Repeater {
                        model: panel.formats
                        MenuItem {
                            required property var modelData
                            objectName: "watchFormatItem_" + modelData.name
                            text: modelData.text
                            checkable: true
                            checked: panel.format === modelData.name
                            ButtonGroup.group: formatItems
                            onTriggered: panel.setFormat(modelData.name)
                        }
                    }
                }
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
                    if (item && item.height > 0)
                        widest = Math.max(widest, item.implicitWidth)
                }
                return widest
            }
            width: Math.max(implicitWidth, widestItem + leftPadding + rightPadding)
            // A style that draws the check inside the item's left padding has
            // kept the column there already (TelemetryDark); one that draws it
            // at the padding's edge pushes the label past it (Basic).
            readonly property real checkColumn: readManyItem.leftPadding
                + (readManyItem.indicator && readManyItem.indicator.x >= readManyItem.leftPadding
                   ? readManyItem.indicator.width + readManyItem.spacing : 0)
            MenuItem {
                objectName: "refreshWatchItem"
                visible: !panel.readOnceInBar
                height: panel.readOnceInBar ? 0 : implicitHeight
                leftPadding: moreMenu.checkColumn
                text: qsTr("Read once")
                // Off while hidden: the menu's arrows do not skip a hidden item.
                enabled: panel.canRead && !panel.readOnceInBar
                onTriggered: panel.requestStart(false)
            }
            MenuItem {
                objectName: "addWatchItem"
                leftPadding: moreMenu.checkColumn
                text: qsTr("Watch the selected Field")
                enabled: panel.provider.canAddWatch === true && !panel.changeLocked
                onTriggered: {
                    // Through the property: setting the title's state would
                    // break its binding to `expanded`.
                    if (panel.provider.addWatch())
                        panel.expanded = true
                }
            }
            MenuItem {
                objectName: "watchViewListItem"
                readonly property bool shown: panel.treeAvailable && !panel.viewInBar
                visible: shown
                height: shown ? implicitHeight : 0
                enabled: shown
                checkable: true
                ButtonGroup.group: viewItems
                checked: !panel.treeView
                text: qsTr("List view")
                onTriggered: panel.showTree(false)
            }
            MenuItem {
                objectName: "watchViewTreeItem"
                readonly property bool shown: panel.treeAvailable && !panel.viewInBar
                visible: shown
                height: shown ? implicitHeight : 0
                enabled: shown
                checkable: true
                ButtonGroup.group: viewItems
                checked: panel.treeView
                text: qsTr("Tree view")
                onTriggered: panel.showTree(true)
            }
            MenuItem {
                objectName: "watchExpandAll"
                visible: panel.treeView
                height: panel.treeView ? implicitHeight : 0
                enabled: panel.treeView && panel.rows.count > 0
                leftPadding: moreMenu.checkColumn
                text: qsTr("Expand all")
                onTriggered: panel.rows.expandAll()
            }
            MenuItem {
                objectName: "watchCollapseAll"
                visible: panel.treeView
                height: panel.treeView ? implicitHeight : 0
                enabled: panel.treeView && panel.rows.count > 0
                leftPadding: moreMenu.checkColumn
                text: qsTr("Collapse all")
                onTriggered: panel.rows.collapseAll()
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
                enabled: moreMenu.choosable && !panel.active
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
                leftPadding: moreMenu.checkColumn
                text: qsTr("Clear the Watchlist…")
                enabled: panel.count > 0 && !panel.changeLocked
                onTriggered: clearDialog.open()
            }
        }

        // The status under the toolbar, wrapped: a failure's advice is read
        // whole, however long, and the toolbar does not move as it changes.
        Label {
            objectName: "watchStatusLabel"
            visible: (panel.statusShown || panel.readManyAdvice) && text.length > 0
            Layout.fillWidth: true
            // The height its text takes, wrapped: the layout missed this
            // label's implicit height growing (a bigger font, a glyph from a
            // fallback font) and cut the last line.
            Layout.preferredHeight: Math.ceil(contentHeight)
            textFormat: Text.PlainText
            text: panel.statusText
            color: panel.readManyAdvice ? Theme.warning : Theme.textSecondary
            font.pointSize: Type.caption
            wrapMode: Text.Wrap
        }
        // The outcome of the last write from a row, in words; or why the row
        // asked to write can't be.
        Label {
            objectName: "watchWriteOutcome"
            visible: panel.open && text.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.ceil(contentHeight)
            textFormat: Text.PlainText
            text: panel.writeRefusalText.length > 0 ? panel.writeRefusalText
                : panel.provider.watchWriteOutcome === undefined ? "" : panel.provider.watchWriteOutcome
            color: Theme.textSecondary
            font.pointSize: Type.caption
            wrapMode: Text.Wrap
        }
        // Qt Quick has no clipboard API; a hidden editor copies its text.
        TextEdit { id: clipboardHelper; visible: false; textFormat: TextEdit.PlainText }
        // What the last removal or copy did, as the Device tree says it:
        // "Removed 3 values of device.Config · Undo", Undo while nothing else
        // changed; "Copied device.Config.Gain".
        RowLayout {
            objectName: "watchNoticeLine"
            visible: panel.open && panel.noticeText.length > 0
            Layout.fillWidth: true
            spacing: Type.spacing
            Label {
                objectName: "watchNotice"
                // As wide as its words, cut only when the line is too narrow.
                Layout.minimumWidth: 0
                textFormat: Text.PlainText
                text: panel.noticeText
                color: Theme.textSecondary
                font.pointSize: Type.caption
                elide: Text.ElideRight
            }
            Label {
                id: removedDot
                visible: removedUndo.visible
                text: "·"
                color: Theme.textMuted
                font.pointSize: Type.caption
                Accessible.ignored: true
            }
            LinkButton {
                id: removedUndo
                objectName: "watchUndo"
                visible: panel.noticeUndo && panel.provider.canUndoWatch === true
                text: qsTr("Undo")
                font.pointSize: Type.caption
                onClicked: panel.undoRemoval()
            }
            Item { Layout.fillWidth: true }
        }
        // Column titles over the one-line rows; they stay while the rows scroll.
        RowLayout {
            objectName: "watchColumns"
            visible: panel.open && panel.count > 0
            Layout.fillWidth: true
            Layout.rightMargin: panel.scrollReserve
            Layout.topMargin: Type.tight
            // The row family's gap: "230" and "OK" read as two things.
            spacing: panel.columnGap
            Label { objectName: "watchColumnName"; text: qsTr("Name"); color: Theme.textSecondary; font.pointSize: Type.caption; Layout.leftMargin: Type.padding; Layout.fillWidth: true; elide: Text.ElideRight }
            Label { objectName: "watchColumnValue"; text: qsTr("Value"); color: Theme.textSecondary; font.pointSize: Type.caption; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: panel.valueColumn; elide: Text.ElideRight }
            Label { objectName: "watchColumnType"; visible: panel.typeColumn > 0; text: qsTr("Type"); color: Theme.textSecondary; font.pointSize: Type.caption; Layout.preferredWidth: panel.typeColumn; elide: Text.ElideRight }
            Label { objectName: "watchColumnAccess"; visible: panel.accessColumn > 0; text: qsTr("rw"); color: Theme.textSecondary; font.pointSize: Type.caption; Layout.preferredWidth: panel.accessColumn
                    ToolTip.visible: accessTitleHover.hovered; ToolTip.delay: 700; ToolTip.text: qsTr("Access: r can be read, w can be written")
                    HoverHandler { id: accessTitleHover } }
            Label { objectName: "watchColumnStatus"; visible: panel.statusColumn > 0; text: panel.statusGlyph ? "" : qsTr("Status"); color: Theme.textSecondary; font.pointSize: Type.caption; Layout.preferredWidth: panel.statusColumn }
            Item { Layout.preferredWidth: panel.removeColumnWidth }
        }
        Rectangle {
            objectName: "watchColumnsRule"
            visible: panel.open && panel.count > 0
            Layout.fillWidth: true
            Layout.rightMargin: panel.scrollReserve
            Layout.preferredHeight: 1
            color: Theme.outlineVariant
        }

        // Folded, the list keeps its place at no height rather than hiding:
        // hidden, then shown with its model in the same change, this panel made
        // its rows twice and a held row was destroyed (Qt 6.12, the inspector's
        // check; not reproduced in a smaller case).
        ListView {
            id: watchList
            onCurrentIndexChanged: panel.writeRefusalText = ""
            objectName: "watchlistView"
            Layout.fillWidth: true
            // With the "+" row the list is as tall as its rows, the row right
            // under the last one; past the room it scrolls.
            Layout.fillHeight: panel.open && !panel.addAvailable
            Layout.preferredHeight: !panel.open ? 0 : panel.addAvailable ? Math.max(1, contentHeight)
                                                                     : Math.max(120, Type.rowHeight * 5)
            Layout.minimumHeight: panel.open && panel.addAvailable ? Math.min(contentHeight, Type.rowHeight * 3) : 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: !panel.open ? null : panel.rows ? panel.rows : panel.provider.watchlist
            // The keyboard: arrows move the current row, Delete removes it,
            // F2 or Enter writes a writable one.
            activeFocusOnTab: panel.open
            keyNavigationEnabled: true
            currentIndex: -1
            onActiveFocusChanged: if (activeFocus && currentIndex < 0 && count > 0) currentIndex = 0
            // Ctrl+Z is the Watchlist's own while it has the keys.
            Keys.onShortcutOverride: event => {
                if (event.key === Qt.Key_Z && event.modifiers === Qt.ControlModifier && panel.provider.canUndoWatch === true)
                    event.accepted = true
                if (event.key === Qt.Key_C && currentIndex >= 0 && (event.modifiers === Qt.ControlModifier
                        || event.modifiers === (Qt.AltModifier | Qt.ShiftModifier)))
                    event.accepted = true
            }
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Z && event.modifiers === Qt.ControlModifier) {
                    event.accepted = panel.undoRemoval()
                    return
                }
                // Insert from anywhere in the list, or Down past the last row: the "+" row.
                if (panel.addAvailable && (event.key === Qt.Key_Insert
                        || (event.key === Qt.Key_Down && event.modifiers === Qt.NoModifier && currentIndex === count - 1))) {
                    addRow.begin()
                    event.accepted = true
                    return
                }
                if (currentIndex < 0)
                    return
                if (event.key === Qt.Key_C && event.modifiers === Qt.ControlModifier) {
                    event.accepted = panel.copyRows(currentIndex, "lines")
                    return
                }
                if (event.key === Qt.Key_C && event.modifiers === (Qt.AltModifier | Qt.ShiftModifier)) {
                    event.accepted = panel.copyRows(currentIndex, "paths")
                    return
                }
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers === Qt.ShiftModifier)) {
                    if (currentItem)
                        panel.openRowMenu(currentIndex, currentItem, Type.padding, currentItem.height)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Delete && !panel.changeLocked) {
                    panel.removeAt(currentIndex)
                    event.accepted = true
                } else if (panel.treeView && currentItem
                           && (event.key === Qt.Key_Right || event.key === Qt.Key_Left)) {
                    // The Device tree's keys: Right opens, then steps in; Left
                    // folds, then steps out.
                    if (event.key === Qt.Key_Right) {
                        if (currentItem.branch && !currentItem.expanded)
                            panel.rows.setExpanded(currentIndex, true)
                        else if (currentItem.branch && currentIndex + 1 < count)
                            currentIndex = currentIndex + 1
                    } else if (currentItem.branch && currentItem.expanded) {
                        panel.rows.setExpanded(currentIndex, false)
                    } else if (panel.rows.parentRow(currentIndex) >= 0) {
                        currentIndex = panel.rows.parentRow(currentIndex)
                    }
                    event.accepted = true
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                           && currentItem && currentItem.branch) {
                    panel.rows.setExpanded(currentIndex, !currentItem.expanded)
                    event.accepted = true
                } else if ((event.key === Qt.Key_F2 || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                           && currentItem && currentItem.writable) {
                    currentItem.startEdit()
                    event.accepted = true
                } else if (event.key === Qt.Key_F2 && currentItem && currentItem.writeRefusal.length > 0) {
                    panel.writeRefusalText = currentItem.writeRefusal
                    event.accepted = true
                }
            }
            ScrollBar.vertical: ScrollBar {
                id: watchScrollBar
                objectName: "watchlistScrollBar"
                policy: ScrollBar.AsNeeded
            }
            // A focus scope: the list gives its current row the focus again
            // when rows come in above it, and the row hands it to its open
            // editor instead of taking it away.
            delegate: FocusScope {
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
                // What the row is: a value, or in the tree a node above values
                // (a group, a struct, an array, an element of one).
                readonly property string node: typeof model.node === "string" ? model.node : "value"
                readonly property bool isValue: node === "value"
                readonly property bool isGroup: node === "group"
                readonly property int depth: typeof model.depth === "number" ? model.depth : 0
                readonly property bool branch: model.branch === true
                readonly property bool expanded: model.expanded === true
                readonly property int leaves: typeof model.leaves === "number" ? model.leaves : 1
                // Inside a Field in the tree: one read, one outcome, shown on
                // the Field; its members leave Status and rw blank.
                readonly property bool nested: model.nested === true
                readonly property int sourceRow: typeof model.sourceRow === "number" ? model.sourceRow : index
                property bool editing: false
                property bool manualCode: false
                // A value inside a struct or array Field is written by writing
                // its whole Field: what that covers, and why it can't be.
                readonly property var writeScope: model.writeScope && typeof model.writeScope.field === "string"
                                                  && model.writeScope.field.length > 0 ? model.writeScope : null
                readonly property string writeRefusal: typeof model.writeRefusal === "string" ? model.writeRefusal : ""
                // The write reads the Field again first: the other values keep
                // what the device holds then, not what the row last showed.
                readonly property string scopeText: {
                    if (!writeScope)
                        return ""
                    const one = writeScope.others === 1
                    switch (writeScope.kind) {
                    case "elements":
                        return one ? qsTr("Writes all of %1; its other element keeps its value.").arg(writeScope.field)
                                   : qsTr("Writes all of %1; its other %L2 elements keep their values.")
                                         .arg(writeScope.field).arg(writeScope.others)
                    case "values":
                        return one ? qsTr("Writes all of %1; its other value keeps its value.").arg(writeScope.field)
                                   : qsTr("Writes all of %1; its other %L2 values keep their values.")
                                         .arg(writeScope.field).arg(writeScope.others)
                    default:
                        return one ? qsTr("Writes all of %1; its other member keeps its value.").arg(writeScope.field)
                                   : qsTr("Writes all of %1; its other %L2 members keep their values.")
                                         .arg(writeScope.field).arg(writeScope.others)
                    }
                }
                readonly property string overwriteText: writeScope
                    ? qsTr("If the device changes another value of %1 in between, that change is overwritten.")
                          .arg(writeScope.field)
                    : ""
                property string draftText: ""
                readonly property string editorKind: typeof model.editorKind === "string" ? model.editorKind : "text"
                readonly property bool choices: editorKind === "bool" || editorKind === "enum"
                readonly property bool choicesTruncated: model.editorChoicesTruncated === true
                readonly property var choiceOptions: {
                    const rows = []
                    const source = model.editorChoices
                    if (source && typeof source.length === "number") {
                        for (let i = 0; i < source.length; ++i) {
                            const item = source[i]
                            if (item && typeof item.label === "string" && typeof item.text === "string")
                                rows.push({label: item.label, text: item.text, manual: false})
                        }
                    }
                    if (editorKind === "enum") {
                        if (draftText.length > 0 && !rows.some(item => item.text === draftText))
                            rows.unshift({label: (choicesTruncated ? qsTr("Code (%1)") : qsTr("Unknown (%1)"))
                                .arg(draftText), text: draftText, manual: false})
                        rows.push({label: qsTr("Enter code…"), text: "", manual: true})
                    }
                    return rows
                }
                readonly property int choiceIndex: choiceOptions.findIndex(item => !item.manual && item.text === draftText)
                function startEdit() {
                    if (!writable && writeRefusal.length > 0) {
                        panel.writeRefusalText = writeRefusal
                        return
                    }
                    if (!writable || editing)
                        return
                    panel.writeRefusalText = ""
                    manualCode = false
                    // New providers own the exact typed text, including enum codes.
                    // Keep the legacy fallback only when the role is absent.
                    draftText = typeof model.editorText === "string" ? model.editorText
                        : model.sampleExact === true
                            ? (model.sampleValue === 0 && 1 / model.sampleValue < 0 ? "-0" : String(model.sampleValue))
                            : watchRow.text
                    valueEditor.text = draftText
                    editing = true
                    if (choices) valueChoice.forceActiveFocus()
                    else { valueEditor.forceActiveFocus(); valueEditor.selectAll() }
                }
                function commitEdit() {
                    if (!editing || !writable || draftText.length === 0)
                        return
                    const value = draftText
                    finishEdit()
                    panel.provider.writeWatch(watchRow.sourceRow, value)
                }
                // The list is a focus scope: the editor lets go of its focus
                // first, or the list would hand it back to the hidden editor.
                function finishEdit(restoreFocus = true) {
                    editing = false
                    valueChoice.popup.close()
                    valueEditor.focus = false
                    valueChoice.focus = false
                    editorScope.focus = false
                    if (restoreFocus) watchList.forceActiveFocus()
                }
                // One line of columns; a row being edited takes a second line
                // for its editor, under its name and ×.
                readonly property bool compact: editing
                // A reading not yet made, and one a newer reading should have
                // replaced (family A6).
                readonly property bool quiet: status === "" || status === "Pending" || status === "Not read" || status === "NotRead"
                readonly property bool waiting: isValue && quiet
                readonly property bool stale: status === "Stale"
                // Numbers, hex and bools line up on the right; enum names and a
                // folded node's summary on the left.
                // The form the row shows: its own, else the Watchlist's. A
                // node's own is the one its values share, "mixed" when not.
                readonly property string ownFormat: typeof model.format === "string" && model.format !== "mixed"
                                                    ? model.format : ""
                readonly property string formatName: ownFormat.length > 0 ? ownFormat : panel.format
                readonly property string shownText: typeof model.shownText === "string" && model.shownText.length > 0
                                                    ? model.shownText : text
                readonly property bool leftValue: !isValue || (editorKind === "enum" && formatName === "original")
                // A word only for a problem or a stale value; waiting is "–" alone,
                // its word in the value's tooltip and the row's description.
                readonly property string statusText: ok || quiet || nested || isGroup ? "" : panel.statusWord(status)
                readonly property string waitingText: waiting ? panel.statusWord(status === "" ? "Not read" : status) : ""
                readonly property string accessText: !nested && typeof model.access === "string" ? model.access : ""
                readonly property string typeText: typeof model.typeText === "string" ? model.typeText : ""
                // Reports this row's widths again when the panel forgot them.
                readonly property int measureEpoch: panel.measureEpoch
                // After the change has settled (a new zoom's fonts); the timer
                // is the row's own, so a row destroyed meanwhile is not called.
                onMeasureEpochChanged: reportAgain.restart()
                Timer { id: reportAgain; interval: 0; onTriggered: watchRow.report() }
                // Numbers never elide; text and summaries do, within the cap.
                function reportValue() {
                    // A node's summary uses the name's room, never the column's.
                    if (!watchRow.isValue)
                        return
                    if (watchRow.leftValue)
                        panel.noteMeasured(valueText.implicitWidth - valueText.leftPadding, 0, 0, false)
                    else
                        panel.noteNumber(valueText.implicitWidth - valueText.leftPadding)
                }
                function report() {
                    watchRow.reportValue()
                    panel.noteMeasured(0,
                                       typeMetrics.text.length > 0 ? typeMetrics.advanceWidth + Type.tight : 0,
                                       statusMetrics.text.length > 0 ? statusMetrics.advanceWidth + Type.tight : 0,
                                       accessText.length > 0)
                }
                readonly property bool ok: status === "Ok"
                // Waiting is not a problem; Stale and failures are.
                readonly property bool problem: !ok && !quiet
                readonly property bool current: ListView.isCurrentItem && watchList.activeFocus
                readonly property bool removeShown: compact || rowHover.hovered || current
                readonly property color foreground: current ? Theme.selectedForeground : Theme.textPrimary
                readonly property real valueHeight: editing ? Math.max(Type.rowHeight, editorScope.implicitHeight) : Type.rowHeight
                // What a keyboard or screen reader user hears: the hidden
                // columns too, and the status the glyph only marks (spec F).
                Accessible.role: panel.treeView ? Accessible.TreeItem : Accessible.ListItem
                Accessible.name: [panel.treeView ? watchRow.name.substring(nameCell.leafStart) : watchRow.name,
                                  watchRow.isGroup ? qsTr("group, %L1 watched").arg(watchRow.leaves)
                                  : watchRow.isValue ? ""
                                  : watchRow.node === "array" ? qsTr("array, %L1 watched").arg(watchRow.leaves)
                                  : watchRow.node === "element" ? qsTr("element, %L1 watched").arg(watchRow.leaves)
                                  : qsTr("struct, %L1 watched").arg(watchRow.leaves),
                                  watchRow.branch ? (watchRow.expanded ? qsTr("expanded") : qsTr("collapsed")) : "",
                                  watchRow.shownText.length > 0 ? watchRow.shownText : watchRow.isValue ? "–" : "",
                                  watchRow.typeText,
                                  watchRow.accessText === "rw" ? qsTr("read and write")
                                  : watchRow.accessText === "r-" ? qsTr("read only") : ""]
                    .filter(part => part.length > 0).join(", ")
                Accessible.description: [watchRow.statusText.length > 0 ? watchRow.statusText : watchRow.waitingText,
                                         watchRow.isValue && watchRow.ownFormat.length > 0
                                             ? qsTr("shown in %1").arg(panel.formatWord(watchRow.ownFormat)) : ""]
                    .filter(part => part.length > 0).join(", ")
                height: compact ? Type.rowHeight + valueHeight : valueHeight
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
                    onDoubleTapped: if (watchRow.branch) panel.rows.setExpanded(watchRow.index, !watchRow.expanded)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        watchList.currentIndex = watchRow.index
                        panel.openRowMenu(watchRow.index, watchRow, eventPoint.position.x, eventPoint.position.y)
                    }
                }
                // A folded node's values in brief, in the name's spare room up
                // to the numbers' edge, so no column moves when a row folds.
                Text {
                    id: summaryText
                    objectName: "watchRowSummary"
                    visible: !watchRow.isValue && !watchRow.isGroup && watchRow.text.length > 0 && width > 0
                    readonly property real start: rowLayout.x + nameCell.x + leafText.x + leafText.width + panel.columnGap
                    x: start
                    y: 0
                    width: Math.max(0, rowLayout.x + valueText.x + valueText.width - start)
                    height: Type.rowHeight
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    textFormat: Text.PlainText
                    text: watchRow.text
                    font.family: Type.fixedFamily
                    font.pointSize: panel.font.pointSize
                    font.italic: watchRow.stale || watchRow.problem
                    color: watchRow.current ? Theme.selectedForeground
                         : watchRow.ok ? Theme.textPrimary : Theme.textMuted
                    elide: Text.ElideRight
                    HoverHandler { id: summaryHover }
                    PlainToolTip { visible: summaryHover.hovered && summaryText.truncated; text: watchRow.text; delay: 700 }
                }
                // The tree's chevron, an indent wide at the row's depth; the
                // indent is kept on values, so names of one depth share an x.
                Item {
                    objectName: "watchRowChevron"
                    visible: panel.treeView && watchRow.branch
                    x: Type.padding + watchRow.depth * Type.indent
                    y: 0
                    width: Type.indent
                    height: Type.rowHeight
                    Accessible.ignored: true
                    Chevron {
                        anchors.centerIn: parent
                        width: Type.iconSize
                        height: Type.iconSize
                        open: watchRow.expanded
                        color: watchRow.current ? Theme.selectedForeground : Theme.textSecondary
                    }
                    TapHandler {
                        onTapped: {
                            watchList.currentIndex = watchRow.index
                            panel.rows.setExpanded(watchRow.index, !watchRow.expanded)
                        }
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
                    columns: watchRow.compact ? 2 : 6
                    rowSpacing: 0
                    columnSpacing: watchRow.compact ? Type.spacing : panel.columnGap
                    // The path: its prefix muted, its last segment whole while
                    // it fits, the prefix eliding in its middle (dev…Config.Gain).
                    Item {
                        id: nameCell
                        objectName: "watchRowName"
                        Layout.row: 0
                        Layout.column: 0
                        Layout.leftMargin: Type.padding + (panel.treeView ? (watchRow.depth + 1) * Type.indent : 0)
                        Layout.fillWidth: true
                        Layout.minimumWidth: 1
                        Layout.preferredHeight: Type.rowHeight
                        readonly property int leafStart: typeof watchRow.model.leafStart === "number"
                                                         ? Math.max(0, Math.min(watchRow.name.length, watchRow.model.leafStart)) : 0
                        Text {
                            id: prefixText
                            objectName: "watchRowPrefix"
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(0, Math.min(implicitWidth, parent.width - leafText.implicitWidth))
                            visible: text.length > 0 && width > 0
                            textFormat: Text.PlainText
                            text: panel.treeView ? "" : watchRow.name.substring(0, nameCell.leafStart)
                            font: panel.font
                            color: watchRow.current ? Theme.selectedForeground : Theme.textMuted
                            elide: Text.ElideMiddle
                        }
                        Text {
                            id: leafText
                            objectName: "watchRowLeaf"
                            anchors.left: prefixText.visible ? prefixText.right : parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, parent.width - (prefixText.visible ? prefixText.width : 0))
                            textFormat: Text.PlainText
                            text: watchRow.name.substring(nameCell.leafStart)
                            font.family: panel.font.family
                            font.pointSize: panel.font.pointSize
                            font.weight: watchRow.isGroup ? Type.headingWeight : Font.Normal
                            color: watchRow.foreground
                            elide: Text.ElideRight
                        }
                        HoverHandler { id: nameHover }
                        PlainToolTip {
                            visible: nameHover.hovered && (prefixText.truncated || leafText.truncated)
                            text: watchRow.name
                            delay: 700
                        }
                    }
                    // A group shows how many values it holds, at the row's right.
                    Label {
                        objectName: "watchGroupCount"
                        visible: watchRow.isGroup
                        Layout.row: 0
                        Layout.column: 1
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: qsTr("%L1 watched").arg(watchRow.leaves)
                        font.pointSize: Type.caption
                        color: watchRow.current ? Theme.selectedForeground : Theme.textMuted
                    }
                    Label {
                        id: valueText
                        objectName: "watchValueText"
                        visible: !watchRow.editing && !watchRow.isGroup
                        Layout.row: 0
                        Layout.column: 1
                        Layout.preferredWidth: panel.valueColumn
                        Layout.minimumWidth: 1
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        // Not read yet: a dash at the value's alignment, no word.
                        text: !watchRow.isValue ? "" : watchRow.shownText.length > 0 ? watchRow.shownText : watchRow.waiting ? "–" : ""
                        font.family: Type.fixedFamily
                        font.italic: (watchRow.stale || watchRow.problem) && watchRow.text.length > 0
                        color: watchRow.current ? Theme.selectedForeground
                             : watchRow.ok ? Theme.textPrimary : Theme.textMuted
                        horizontalAlignment: watchRow.leftValue ? Text.AlignLeft : Text.AlignRight
                        leftPadding: watchRow.leftValue ? panel.pencilRoom : 0
                        elide: Text.ElideRight
                        onImplicitWidthChanged: watchRow.reportValue()
                        Component.onCompleted: watchRow.reportValue()
                        ToolTip.visible: valueHover.hovered && (truncated || watchRow.waitingText.length > 0
                                                                || watchRow.ownFormat.length > 0)
                        ToolTip.text: watchRow.waitingText.length > 0 ? watchRow.waitingText
                                    : watchRow.ownFormat.length === 0 ? watchRow.shownText
                                    : watchRow.ownFormat === panel.format
                                      ? qsTr("%1 · shown in %2").arg(watchRow.shownText).arg(panel.formatWord(watchRow.ownFormat))
                                    : qsTr("%1 · shown in %2 (the Watchlist shows %3)").arg(watchRow.shownText)
                                          .arg(panel.formatWord(watchRow.ownFormat)).arg(panel.formatWord(panel.format))
                        HoverHandler { id: valueHover }
                        TapHandler {
                            enabled: watchRow.writable || watchRow.writeRefusal.length > 0
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
                    FocusScope {
                        id: editorScope
                        visible: watchRow.editing
                        Layout.row: watchRow.compact ? 1 : 0
                        Layout.column: watchRow.compact ? 0 : 1
                        Layout.columnSpan: watchRow.compact ? 2 : 1
                        Layout.fillWidth: true
                        Layout.minimumWidth: Type.bodyPixels * 4.5
                        Layout.preferredHeight: implicitHeight
                        implicitHeight: editorColumn.implicitHeight
                        onActiveFocusChanged: if (!activeFocus && watchRow.editing) cancelEdit.restart()
                        Timer {
                            id: cancelEdit
                            interval: 0
                            onTriggered: if (watchRow.editing && !editorScope.activeFocus && !valueChoice.popup.visible)
                                watchRow.finishEdit(false)
                        }
                        Timer {
                            id: manualFocus
                            interval: 0
                            onTriggered: if (watchRow.editing && watchRow.manualCode) {
                                valueEditor.forceActiveFocus()
                                valueEditor.selectAll()
                            }
                        }
                        ColumnLayout {
                            id: editorColumn
                            anchors.fill: parent
                            spacing: 0
                            RowLayout {
                                id: editorLine
                                Layout.fillWidth: true
                                spacing: Type.tight
                                ComboBox {
                                    id: valueChoice
                                    objectName: "watchValueChoice"
                                    visible: watchRow.choices && !watchRow.manualCode
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    model: watchRow.choiceOptions
                                    textRole: "label"
                                    valueRole: "text"
                                    currentIndex: watchRow.choiceIndex
                                    displayText: currentIndex < 0 ? qsTr("Choose a value") : currentText
                                    palette.button: Theme.buttonFill
                                    palette.buttonText: Theme.textPrimary
                                    palette.text: Theme.textPrimary
                                    palette.base: Theme.surface
                                    palette.light: Theme.stateSelected
                                    palette.midlight: Theme.stateHover
                                    palette.mid: Theme.buttonFillPressed
                                    palette.dark: Theme.textSecondary
                                    palette.highlight: Theme.stateSelected
                                    palette.highlightedText: Theme.selectedForeground
                                    delegate: ItemDelegate {
                                        id: choiceOption
                                        required property var model
                                        required property int index
                                        width: ListView.view ? ListView.view.width : implicitWidth
                                        text: model.label
                                        highlighted: valueChoice.highlightedIndex === index
                                        font.weight: valueChoice.currentIndex === index ? Type.headingWeight : Type.bodyWeight
                                        palette.text: Theme.textPrimary
                                        palette.highlightedText: Theme.selectedForeground
                                        palette.light: Theme.stateSelected
                                        palette.midlight: Theme.stateHover
                                        palette.highlight: Theme.stateSelected
                                        PlainToolTip { visible: choiceOption.hovered; text: choiceOption.text; delay: 700 }
                                    }
                                    Accessible.name: qsTr("New value for %1").arg(watchRow.name)
                                    Accessible.description: currentIndex < 0
                                        ? qsTr("Choose a value, then Write or press Enter. Escape cancels.")
                                        : qsTr("%1. Choose a value, then Write or press Enter. Escape cancels.").arg(currentText)
                                    PlainToolTip {
                                        objectName: "watchValueChoiceToolTip"
                                        visible: (valueChoice.hovered || valueChoice.visualFocus) && !valueChoice.popup.visible
                                        delay: 700
                                        text: valueChoice.Accessible.description
                                    }
                                    onActivated: function(index) {
                                        const item = watchRow.choiceOptions[index]
                                        if (!item) return
                                        if (item.manual) {
                                            valueEditor.text = watchRow.draftText
                                            watchRow.manualCode = true
                                            manualFocus.restart()
                                        } else watchRow.draftText = item.text
                                        currentIndex = Qt.binding(() => watchRow.choiceIndex)
                                    }
                                    Keys.onReturnPressed: event => {
                                        if (popup.visible) event.accepted = false
                                        else watchRow.commitEdit()
                                    }
                                    Keys.onEnterPressed: event => {
                                        if (popup.visible) event.accepted = false
                                        else watchRow.commitEdit()
                                    }
                                    Keys.onEscapePressed: watchRow.finishEdit()
                                    Connections {
                                        target: valueChoice.popup
                                        function onClosed() { cancelEdit.restart() }
                                    }
                                }
                                TextField {
                                    id: valueEditor
                                    objectName: "watchValueEditor"
                                    visible: !watchRow.choices || watchRow.manualCode
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    Layout.preferredHeight: Math.max(Type.rowHeight, implicitHeight)
                                    font.family: Type.fixedFamily
                                    horizontalAlignment: watchRow.compact ? TextInput.AlignLeft : TextInput.AlignRight
                                    placeholderText: watchRow.manualCode ? qsTr("Numeric code") : ""
                                    topPadding: Type.tight
                                    bottomPadding: Type.tight
                                    onTextEdited: watchRow.draftText = text
                                    Keys.onReturnPressed: watchRow.commitEdit()
                                    Keys.onEnterPressed: watchRow.commitEdit()
                                    Keys.onEscapePressed: watchRow.finishEdit()
                                    Accessible.name: qsTr("New value for %1").arg(watchRow.name)
                                    Accessible.description: watchRow.writeScope
                                        ? qsTr("%1. %2 %3 Write all or press Enter to send it once. Escape cancels.")
                                            .arg(watchRow.draftText).arg(watchRow.scopeText).arg(watchRow.overwriteText)
                                        : qsTr("%1. Write or press Enter to send this value once. Escape cancels.").arg(watchRow.draftText)
                                    PlainToolTip {
                                        objectName: "watchValueEditorToolTip"
                                        visible: valueEditor.hovered || valueEditor.activeFocus
                                        delay: 700
                                        text: valueEditor.Accessible.description
                                    }
                                }
                                ToolButton {
                                    objectName: "watchValueChoiceReturn"
                                    visible: watchRow.manualCode
                                    text: "⌄"
                                    Accessible.name: qsTr("Choose a named value for %1").arg(watchRow.name)
                                    onClicked: {
                                        watchRow.manualCode = false
                                        valueChoice.forceActiveFocus()
                                        valueChoice.popup.open()
                                    }
                                }
                                Button {
                                    objectName: "watchValueWrite"
                                    Layout.preferredWidth: implicitContentWidth + leftPadding + rightPadding
                                    text: watchRow.writeScope ? qsTr("Write all") : qsTr("Write")
                                    enabled: watchRow.writable && watchRow.draftText.length > 0
                                    Accessible.name: qsTr("Write %1 once").arg(watchRow.name)
                                    Accessible.description: watchRow.scopeText
                                    onClicked: watchRow.commitEdit()
                                }
                            }
                            // Line 2 of a value's editor: what Write all covers.
                            Label {
                                objectName: "watchWriteScope"
                                visible: watchRow.writeScope !== null
                                Layout.fillWidth: true
                                Layout.leftMargin: Type.padding
                                Layout.preferredHeight: visible ? Math.max(Type.rowHeight, Math.ceil(contentHeight) + 2 * Type.tight) : 0
                                verticalAlignment: Text.AlignVCenter
                                textFormat: Text.PlainText
                                text: watchRow.scopeText
                                // The edited row is the current one: all of it in its selected colour.
                                color: watchRow.current ? Theme.selectedForeground : Theme.textSecondary
                                font.pointSize: Type.caption
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                HoverHandler { id: scopeHover }
                                PlainToolTip {
                                    visible: scopeHover.hovered && watchRow.writeScope !== null
                                    delay: 700
                                    text: watchRow.writeScope
                                        ? qsTr("%1 is written whole: Write all reads it, sets %2 in it, writes all of it and reads it back. %3")
                                            .arg(watchRow.writeScope.field).arg(watchRow.name).arg(watchRow.overwriteText)
                                        : ""
                                }
                            }
                        }
                    }
                    Label {
                        objectName: "watchRowType"
                        visible: !watchRow.editing && !watchRow.isGroup && panel.typeColumn > 0
                        Layout.row: 0
                        Layout.column: 2
                        Layout.preferredWidth: panel.typeColumn
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: watchRow.typeText
                        font.family: Type.fixedFamily
                        font.pointSize: Type.caption
                        color: watchRow.current ? Theme.selectedForeground : Theme.textSecondary
                        elide: Text.ElideRight
                        HoverHandler { id: typeHover }
                        PlainToolTip { visible: typeHover.hovered && parent.truncated; text: watchRow.typeText; delay: 700 }
                    }
                    // rw: each letter, a missing right as "-" in the placeholder colour.
                    Row {
                        objectName: "watchRowAccess"
                        visible: !watchRow.editing && !watchRow.isGroup && panel.accessColumn > 0
                        Layout.row: 0
                        Layout.column: 3
                        Layout.preferredWidth: panel.accessColumn
                        Layout.preferredHeight: Type.rowHeight
                        Repeater {
                            model: watchRow.accessText.split("")
                            Text {
                                required property string modelData
                                height: Type.rowHeight
                                verticalAlignment: Text.AlignVCenter
                                text: modelData
                                font.family: Type.fixedFamily
                                font.pointSize: Type.caption
                                color: watchRow.current ? Theme.selectedForeground
                                     : modelData === "-" ? Theme.textDisabled : Theme.textSecondary
                            }
                        }
                        HoverHandler { id: accessHover }
                        PlainToolTip {
                            visible: accessHover.hovered && watchRow.accessText.length > 0
                            text: watchRow.accessText === "rw" ? qsTr("read and write") : qsTr("read only")
                            delay: 700
                        }
                    }
                    // A reading's word only when it is not a fresh value: a
                    // problem in warning, waiting in muted; the provider's
                    // own words in the tooltip.
                    Label {
                        objectName: "watchRowStatus"
                        visible: !watchRow.editing && !watchRow.isGroup && panel.statusColumn > 0 && !panel.statusGlyph
                        Layout.row: 0
                        Layout.column: 4
                        Layout.minimumWidth: 1
                        Layout.preferredWidth: panel.statusColumn
                        Layout.preferredHeight: Type.rowHeight
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: watchRow.statusText
                        color: watchRow.current ? Theme.selectedForeground
                             : watchRow.problem ? Theme.warning : Theme.textMuted
                        font.pointSize: Type.caption
                        elide: Text.ElideRight
                        ToolTip.visible: statusRowHover.hovered && text.length > 0 && (truncated || text !== watchRow.status)
                        ToolTip.text: watchRow.status
                        HoverHandler { id: statusRowHover }
                    }
                    // Too little room: the word becomes a mark, the word in its tooltip.
                    Item {
                        objectName: "watchRowStatusGlyph"
                        visible: !watchRow.editing && !watchRow.isGroup && panel.statusGlyph
                        Layout.row: 0
                        Layout.column: 4
                        Layout.preferredWidth: panel.statusColumn
                        Layout.preferredHeight: Type.rowHeight
                        Text {
                            anchors.centerIn: parent
                            visible: watchRow.statusText.length > 0
                            text: watchRow.problem ? "!" : "·"
                            font.bold: true
                            color: watchRow.current ? Theme.selectedForeground
                                 : watchRow.problem ? Theme.warning : Theme.textSecondary
                        }
                        HoverHandler { id: glyphHover }
                        PlainToolTip { visible: glyphHover.hovered && watchRow.statusText.length > 0; text: watchRow.statusText; delay: 700 }
                    }
                    // Measure the type and the word for their columns, shown or
                    // not: a hidden Text does not lay itself out, so a column
                    // measured from its own label would never appear.
                    TextMetrics {
                        id: typeMetrics
                        font.family: Type.fixedFamily
                        font.pointSize: Type.caption
                        text: watchRow.typeText
                        onAdvanceWidthChanged: panel.noteMeasured(0, text.length > 0 ? advanceWidth + Type.tight : 0, 0,
                                                                  watchRow.accessText.length > 0)
                        Component.onCompleted: panel.noteMeasured(0, text.length > 0 ? advanceWidth + Type.tight : 0, 0,
                                                                  watchRow.accessText.length > 0)
                    }
                    TextMetrics {
                        id: statusMetrics
                        font.family: panel.font.family
                        font.pointSize: Type.caption
                        text: watchRow.statusText
                        onAdvanceWidthChanged: panel.noteMeasured(0, 0, text.length > 0 ? advanceWidth + Type.tight : 0, false)
                    }
                    // × shows on the row under the pointer and on the current
                    // row; the keyboard removes with Delete.
                    Item {
                        Layout.row: 0
                        Layout.column: watchRow.compact ? 1 : 5
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
                            enabled: !panel.changeLocked
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
                            onClicked: panel.removeAt(watchRow.index)
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
                visible: panel.open && panel.count === 0 && !panel.addAvailable
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: Theme.textMuted
                text: !panel.connected ? qsTr("Connect to a device to watch its Fields.")
                    : panel.treeBoxes ? qsTr("Tick Fields in the Device tree to watch them.")
                    : qsTr("Select a Field in the Device tree, then choose Watch the selected Field in the Watchlist's menu.")
            }
        }
        WatchAddRow {
            id: addRow
            visible: panel.open && panel.addAvailable
            Layout.fillWidth: true
            Layout.rightMargin: panel.scrollReserve
            Layout.minimumHeight: implicitHeight
            Layout.preferredHeight: implicitHeight
            provider: panel.provider
            onAdded: (notice, undoable) => {
                panel.noticeText = notice
                panel.noticeUndo = undoable
                // The last value added, its parents opened, in view.
                const last = panel.count - 1
                const row = panel.rows ? panel.rows.reveal(last) : last
                if (row >= 0)
                    watchList.positionViewAtIndex(row, ListView.Contain)
            }
            onReveal: (row, notice) => {
                const shown = panel.rows ? panel.rows.reveal(row) : row
                if (shown >= 0) {
                    watchList.currentIndex = shown
                    watchList.positionViewAtIndex(shown, ListView.Contain)
                }
                panel.noticeText = notice
                panel.noticeUndo = false
            }
            onLeaveUp: {
                watchList.forceActiveFocus()
                watchList.currentIndex = watchList.count - 1
            }
        }
        // Under the "+" row: the room left, and while the list is empty what to do.
        // An open list asks for the room it always did, rows and "+" row included.
        Item {
            visible: panel.open && panel.addAvailable
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.max(0, Math.max(120, Type.rowHeight * 5)
                                                - watchList.Layout.preferredHeight - addRow.implicitHeight)
            Label {
                objectName: "watchEmptyAdd"
                visible: panel.count === 0
                x: Type.padding + Type.indent + Type.tight
                y: Type.tight
                width: parent.width - x - Type.padding
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: Theme.textMuted
                text: panel.connected ? qsTr("Tick Fields in the Device tree to watch them, or type a path.")
                                      : qsTr("Connect to a device to watch its Fields.")
            }
        }
    }

    // The two read modes: one is always chosen.
    ButtonGroup { id: modes; exclusive: true }
    // So are the two views, in the menu.
    ButtonGroup { id: viewItems; exclusive: true }

    // A row's menu (right-click, the Menu key or Shift+F10): its number
    // format, which a node gives every value under it, and removing it.
    Menu {
        id: rowMenu
        objectName: "watchRowMenu"
        readonly property var item: panel.menuRow >= 0 ? watchList.itemAtIndex(panel.menuRow) : null
        readonly property string own: item && typeof item.model.format === "string" ? item.model.format : ""
        Menu {
            id: rowFormatMenu
            objectName: "watchRowFormatMenu"
            title: qsTr("Number format")
            enabled: panel.formatAvailable && typeof panel.provider.setWatchRowFormat === "function"
            ButtonGroup { id: rowFormatItems; exclusive: true }
            MenuItem {
                objectName: "watchRowFormat_inherit"
                text: qsTr("Like the Watchlist (%1)").arg(panel.formatWord(panel.format))
                checkable: true
                checked: rowMenu.own.length === 0
                ButtonGroup.group: rowFormatItems
                onTriggered: panel.formatRows(panel.menuRow, "")
            }
            Repeater {
                model: panel.formats
                MenuItem {
                    required property var modelData
                    objectName: "watchRowFormat_" + modelData.name
                    text: modelData.text
                    checkable: true
                    checked: rowMenu.own === modelData.name
                    ButtonGroup.group: rowFormatItems
                    onTriggered: panel.formatRows(panel.menuRow, modelData.name)
                }
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "watchRowCopy"
            text: qsTr("Copy") + "\t" + qsTr("Ctrl+C")
            onTriggered: panel.copyRows(panel.menuRow, "lines")
        }
        MenuItem {
            objectName: "watchRowCopyValue"
            text: qsTr("Copy value")
            enabled: rowMenu.item !== null && rowMenu.item.isValue && rowMenu.item.shownText.length > 0
            onTriggered: panel.copyRows(panel.menuRow, "value")
        }
        MenuItem {
            objectName: "watchRowCopyPath"
            text: qsTr("Copy path") + "\t" + qsTr("Alt+Shift+C")
            onTriggered: panel.copyRows(panel.menuRow, "paths")
        }
        MenuSeparator {}
        MenuItem {
            objectName: "watchRowRemove"
            text: qsTr("Remove") + "\t" + qsTr("Delete")
            enabled: !panel.changeLocked
            onTriggered: panel.removeAt(panel.menuRow)
        }
    }

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
        // No body: the question is the title, the answer the buttons.
        topPadding: 0
        bottomPadding: 0
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
        onOpened: clearCancel.forceActiveFocus(Qt.TabFocusReason)
    }
}
