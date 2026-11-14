// The values codec's view: which descriptor the file was written with (its
// fingerprint, with Copy), what it holds, and every Field as a tree with its
// value - group, Field, a struct's members, an array's elements. The tree
// opens folded; what is below a row is made when it is opened, and a long
// array opens as ranges of 256 - [0 … 255], [256 … 511], … - as Eclipse shows
// one, so a file of megabytes opens at once and any element is a few levels down. Live reads
// the device's file again and again - never more often than every 200 ms, and
// never before the last read has ended; Hold keeps what is shown while the
// reads go on, and shows the newest on release. The values that changed in
// the latest snapshot are marked. Find narrows the tree; Copy takes a value,
// a path, or a subtree as text, JSON or TSV. MIT.
//
// FileViewer loads it with `decoded`, a ValuesContent, and `content`, the
// FileContent, whose `live` reads the device's file when there is one. An
// index is taken from the selection when it is used, never kept.
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQml.Models

FocusScope {
    id: view
    objectName: "valuesView"

    required property var decoded
    property var content: null
    property bool retiring: false
    function prepareForUnload() {
        if (retiring)
            return
        // First suppress callbacks/focus and hidden-view resume, then stop the
        // existing reader. One accepted read may still finish in the session.
        retiring = true
        pausedWhileHidden = false
        if (findLater)
            findLater.stop()
        if (showLater)
            showLater.stop()
        if (content && typeof content.stopLive === "function")
            content.stopLive()
        else if (live)
            live.running = false
        if (nodeMenu)
            nodeMenu.close()
        if (descriptorDialog)
            descriptorDialog.close()
    }
    // Null for a file from disk: then there is nothing to read again.
    readonly property var live: content ? content.live : null
    readonly property bool running: !!live && live.running

    readonly property real valueWidth: Math.min(260, Math.max(Math.round(Type.bodyPixels * 6), width * 0.3))
    // Type as narrow as its short words allow, so it stays at most widths;
    // the access two letters under "rw", the first to go (family_spec A3).
    readonly property real typeWidth: Math.min(Math.round(Type.bodyPixels * 9.6), Math.max(Math.round(Type.bodyPixels * 4.5), width * 0.16))
    readonly property bool typeShown: width >= Type.bodyPixels * 22
    readonly property real accessWidth: width >= Type.bodyPixels * 28 ? Math.ceil(accessMetrics.advanceWidth) + 2 : 0
    TextMetrics { id: accessMetrics; font.family: Type.fixedFamily; font.pointSize: Type.caption; text: "rw" }
    // For enabling the menu only; actions read current() when they run.
    readonly property bool hasCurrent: tree.currentRow >= 0
    property alias findText: findField.text
    // Find what the field holds now: the matches counted, the tree shown to them.
    function applyFind() {
        if (retiring)
            return
        findLater.stop()
        const text = findField.text
        const found = view.decoded.setFilter(text)
        const limited = text.length > 0 && view.decoded.findLimited()
        matches.limited = limited
        matches.text = text.length === 0 ? ""
                     : limited ? (found === 0 ? qsTr("No match within Find's bounds · narrow the search")
                                              : qsTr("%L1+ matches · narrow the search").arg(found))
                     : found === 1 ? qsTr("1 match") : qsTr("%L1 matches").arg(found)
        showLater.restart()
    }
    // Said after a copy that stopped at its bound; the next copy clears it.
    property string copyNote: ""
    Timer {
        id: findLater
        interval: 200
        onTriggered: view.applyFind()
    }
    // The tree shown to the matches once the rows have settled. A timer of the
    // view's own, not Qt.callLater: closing the tab takes it along.
    Timer {
        id: showLater
        interval: 0
        onTriggered: view.showFound()
    }

    // Read again and again only while shown: a tab behind another, or a
    // closed Viewer, reads nothing, and Live comes back with it.
    property bool pausedWhileHidden: false
    onVisibleChanged: {
        if (retiring || (content && content.retiring === true) || !live)
            return
        if (!visible && live.running) {
            pausedWhileHidden = true
            live.running = false
        } else if (visible && pausedWhileHidden) {
            pausedWhileHidden = false
            live.running = true
        }
    }
    // External lifetime work belongs before Loader clears this context.
    Component.onDestruction: retiring = true

    function focusContent() { if (!retiring) tree.forceActiveFocus() }
    function current() { return tree.selectionModel.currentIndex }
    function copyAs(format, whole) {
        const text = whole ? view.decoded.textOfAll(format) : view.decoded.textOf(view.current(), format)
        if (text.length > 0)
            Clipboard.setText(text)
        view.copyNote = view.decoded.copyNote()
    }
    // A value alone; a struct, an array or a group as indented text.
    function copyValue() {
        const value = view.decoded.valueOf(view.current())
        if (value.length > 0)
            Clipboard.setText(value)
        else
            view.copyAs("text", false)
    }
    function toggleHold() {
        if (view.running || view.decoded.held)
            view.decoded.held = !view.decoded.held
    }
    // A range has no path of its own: nothing is copied, the clipboard kept.
    function copyPath() {
        const path = view.decoded.pathOf(view.current())
        if (path.length > 0)
            Clipboard.setText(path)
    }
    function openMenu(index, from, x, y) {
        if (index && index.valid)
            tree.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
        nodeMenu.popup(from, x, y)
    }
    function timeText(time) {
        return isNaN(time) ? "" : Qt.formatTime(time, "HH:mm:ss.zzz")
    }
    // After Find: the matches' parents open, the matches folded. Cleared:
    // folded again, as it opened, with the current row in view.
    function showFound() {
        if (retiring || !tree)
            return
        if (view.findText.length === 0) {
            tree.collapseRecursively()
            const index = view.current()
            if (index.valid) {
                tree.expandToIndex(index)
                tree.positionViewAtIndex(index, TableView.Contain)
            }
            return
        }
        let matchDepth = -1
        for (let row = 0; row < tree.rows; ++row) {
            const depth = tree.depth(row)
            if (matchDepth >= 0 && depth > matchDepth)
                continue
            matchDepth = -1
            if (view.decoded.matches(tree.index(row, 0)))
                matchDepth = depth
            else if (!tree.isExpanded(row))
                tree.expand(row)
        }
    }

    // Why Live stopped by itself: a snapshot that is not of this descriptor
    // (the device now runs other firmware).
    property string stoppedBecause: ""
    // Each whole read of the device's file, shown unless held.
    Connections {
        target: view.live
        enabled: !view.retiring && !(view.content && view.content.retiring === true)
        function onSnapshot(bytes) {
            if (view.retiring)
                return
            const problem = view.decoded.update(bytes)
            if (problem.length > 0) {
                view.live.running = false
                view.stoppedBecause = qsTr("Live stopped: the device's file is %1").arg(problem)
            }
        }
        function onStateChanged() { if (!view.retiring && view.live && view.live.running) view.stoppedBecause = "" }
    }

    Menu {
        id: nodeMenu
        objectName: "valuesMenu"
        MenuItem {
            objectName: "valuesCopyValue"
            text: qsTr("&Copy value\tCtrl+C")
            enabled: view.hasCurrent
            onTriggered: view.copyValue()
        }
        MenuItem {
            objectName: "valuesCopyPath"
            text: qsTr("Copy &path\tAlt+Shift+C")
            enabled: view.hasCurrent
            onTriggered: view.copyPath()
        }
        MenuItem {
            objectName: "valuesCopyRow"
            text: qsTr("Copy &row")
            enabled: view.hasCurrent
            onTriggered: Clipboard.setText(view.decoded.rowOf(view.current()))
        }
        Menu {
            title: qsTr("Copy &as")
            MenuItem { text: qsTr("&Text\tCtrl+Shift+C"); onTriggered: view.copyAs("text", false) }
            MenuItem { text: qsTr("&JSON"); onTriggered: view.copyAs("json", false) }
            MenuItem { text: qsTr("T&SV"); onTriggered: view.copyAs("tsv", false) }
        }
        MenuSeparator {}
        Menu {
            title: view.findText.length > 0 ? qsTr("Copy shown a&s") : qsTr("Copy a&ll as")
            MenuItem { text: qsTr("&Text"); onTriggered: view.copyAs("text", true) }
            MenuItem { text: qsTr("&JSON"); onTriggered: view.copyAs("json", true) }
            MenuItem { text: qsTr("T&SV"); onTriggered: view.copyAs("tsv", true) }
        }
        MenuSeparator { visible: !!view.live }
        MenuItem {
            visible: !!view.live
            height: visible ? implicitHeight : 0
            text: view.decoded.held ? qsTr("&Release\tH") : qsTr("&Hold\tH")
            enabled: view.running || view.decoded.held
            onTriggered: view.toggleHold()
        }
        onClosed: if (!view.retiring) tree.forceActiveFocus()
    }

    FileDialog {
        id: descriptorDialog
        title: qsTr("Choose the descriptor this file was written with")
        nameFilters: [qsTr("Descriptor (*.bin)"), qsTr("All files (*)")]
        onAccepted: if (!view.retiring) descriptorProblem.text = view.decoded.useDescriptor(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Type.tight

        // One caption line: how many Fields, then the descriptor the file was
        // written with - its fingerprint, with Copy. The mode's segment above
        // already says Values.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.spacing
            Label {
                objectName: "valuesSummary"
                visible: view.decoded.valid
                // Its own width, never more; narrow, it gives way (elided)
                // before Copy does. The fingerprint takes what is left.
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: view.live ? view.decoded.summary
                                : qsTr("%1 · read %2").arg(view.decoded.summary).arg(view.timeText(view.decoded.shownAt))
            }
            Label {
                text: view.decoded.valid ? qsTr("· fingerprint") : qsTr("Fingerprint")
                font.pointSize: Type.caption
                color: Theme.textSecondary
            }
            Label {
                objectName: "valuesFingerprint"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.family: Type.fixedFamily
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: view.decoded.fingerprintGroups
                HoverHandler { id: fingerprintHover }
                ToolTip.visible: fingerprintHover.hovered
                ToolTip.text: qsTr("The fingerprint of the descriptor this file was written with, stored at 0x0010")
            }
            LinkButton {
                objectName: "valuesCopyFingerprint"
                tint: Theme.textSecondary
                text: qsTr("Copy")
                Accessible.name: qsTr("Copy the fingerprint")
                onClicked: Clipboard.setText(view.decoded.fingerprintGroups.replace(/ /g, ""))
            }
        }

        // Without its descriptor the values cannot be read: say which one, and let it be chosen.
        ColumnLayout {
            objectName: "valuesNeedsDescriptor"
            visible: view.decoded.needsDescriptor
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.padding
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textPrimary
                text: qsTr("No descriptor with this fingerprint was found, so these values cannot be read.")
            }
            Label {
                objectName: "valuesNeedsHint"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                text: view.decoded.connectedFingerprintGroups.length > 0
                      ? qsTr("The connected device uses descriptor %1, not this one. Choose the descriptor.bin downloaded with this file.")
                            .arg(view.decoded.connectedFingerprintGroups)
                      : qsTr("Connect to the device that wrote this file, or choose its descriptor.bin. One downloaded together with this file will match.")
            }
            Button {
                objectName: "valuesChooseDescriptor"
                text: qsTr("Choose descriptor…")
                onClicked: descriptorDialog.open()
            }
            Label {
                id: descriptorProblem
                objectName: "valuesDescriptorProblem"
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.Wrap
                color: Theme.error
            }
            Item { Layout.fillHeight: true }
        }

        // Read again and again, in the Watchlist's words: Start / Stop and
        // Every; Hold keeps what is shown while reading goes on.
        RowLayout {
            objectName: "valuesLiveBar"
            visible: view.decoded.valid && !!view.live
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.padding
            // Start or Stop, as wide as the longer word so the bar never moves;
            // the state is the reader's, never the button's own.
            Button {
                id: liveButton
                objectName: "valuesLive"
                TextMetrics { id: startWord; font: liveButton.font; text: qsTr("Start") }
                TextMetrics { id: stopWord; font: liveButton.font; text: qsTr("Stop") }
                Layout.preferredWidth: Math.max(startWord.advanceWidth, stopWord.advanceWidth)
                                       + leftPadding + rightPadding
                text: view.running ? qsTr("Stop") : qsTr("Start")
                // The spoken name begins with the word shown, so speech finds it.
                Accessible.name: view.running ? qsTr("Stop reading the device's file")
                                              : qsTr("Start reading the device's file again and again")
                onClicked: view.live.running = !view.running
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("Read the device's file again and again")
            }
            Label {
                text: qsTr("Every")
                color: Theme.textMuted
                font.pointSize: Type.caption
            }
            ComboBox {
                id: period
                objectName: "valuesPeriod"
                Accessible.name: qsTr("How often to read")
                // As wide as its widest choice, whatever the style's minimum -
                // the Watchlist's rule: the bar never moves.
                implicitContentWidthPolicy: ComboBox.WidestText
                Layout.preferredWidth: implicitContentWidth + leftPadding + rightPadding
                textRole: "text"
                valueRole: "ms"
                // The fastest reads as soon as the last read ended (200 ms at
                // least between starts); the others wait that long after it.
                model: [
                    { text: qsTr("Continuous"), ms: 200 },
                    { text: qsTr("0.5 s"), ms: 500 },
                    { text: qsTr("1 s"), ms: 1000 },
                    { text: qsTr("2 s"), ms: 2000 },
                    { text: qsTr("5 s"), ms: 5000 },
                    { text: qsTr("10 s"), ms: 10000 }
                ]
                Component.onCompleted: currentIndex = Math.max(0, indexOfValue(view.live ? view.live.periodMs : 1000))
                onActivated: if (view.live) view.live.periodMs = currentValue
            }
            // A disabled button takes no hover: the wrapper says why it is off.
            Item {
                implicitWidth: holdButton.implicitWidth
                implicitHeight: holdButton.implicitHeight
                Button {
                    id: holdButton
                    objectName: "valuesHold"
                    anchors.fill: parent
                    text: qsTr("Hold")
                    checked: view.decoded.held
                    Accessible.checkable: true
                    Accessible.checked: view.decoded.held
                    Accessible.description: enabled ? "" : qsTr("Hold needs Live on")
                    enabled: view.running || view.decoded.held
                    onClicked: view.toggleHold()
                }
                HoverHandler { id: holdHover }
                ToolTip.visible: holdHover.hovered
                ToolTip.text: holdButton.enabled ? qsTr("Keep these values on screen while reading goes on (H)")
                                                 : qsTr("Hold needs Live on")
            }
            Item { Layout.fillWidth: true }
        }
        // What Live is doing, on a line of its own: a failed read leaves the
        // old values shown, and this says so.
        RowLayout {
            visible: view.decoded.valid && !!view.live
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            Label {
                objectName: "valuesLiveState"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.pointSize: Type.caption
                // Live stopped by itself is worth a warning; waiting for the
                // device to come back is routine.
                color: view.stoppedBecause.length > 0 || (view.live && view.live.problem.length > 0 && !view.live.waitingForDevice)
                       ? Theme.warning : Theme.textSecondary
                text: {
                    const shown = view.timeText(view.decoded.shownAt)
                    if (!view.live)
                        return ""
                    if (view.stoppedBecause.length > 0)
                        return view.stoppedBecause
                    // Stopped by itself: a refusal, another device.
                    if (view.live.problem.length > 0 && !view.live.running)
                        return qsTr("Live stopped: %1 · showing values read at %2").arg(view.live.problem).arg(shown)
                    // The values shown stay; say how old they are, and what happens next.
                    if (view.live.problem.length > 0 && view.live.running)
                        return view.live.waitingForDevice
                            ? qsTr("Not connected · values from %1 · live again on reconnect").arg(shown)
                            : qsTr("Read failed: %1 · showing values read at %2").arg(view.live.problem).arg(shown)
                    if (view.decoded.held)
                        return view.decoded.waiting === 0 ? qsTr("Held at %1 · H to release").arg(shown)
                             : view.decoded.waiting === 1 ? qsTr("Held at %1 · 1 newer read · H to release").arg(shown)
                             : qsTr("Held at %1 · %2 newer reads · H to release").arg(shown).arg(view.decoded.waiting)
                    const changed = view.decoded.changedCount === 1 ? qsTr("1 value changed")
                                  : qsTr("%1 values changed").arg(view.decoded.changedCount)
                    return view.running ? qsTr("Read at %1 · %2 · took %3 ms").arg(shown).arg(changed).arg(view.live.lastReadMs)
                                        : qsTr("Read at %1").arg(shown)
                }
            }
        }

        // Find: the matches and their parents, the rest folded away.
        RowLayout {
            visible: view.decoded.valid
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.padding
            TextField {
                id: findField
                objectName: "valuesFind"
                Layout.fillWidth: true
                Layout.maximumWidth: 320
                placeholderText: qsTr("Find by name or path (Ctrl+F)")
                Accessible.name: qsTr("Find by name or path")
                // Typing waits a moment before finding: in a file of megabytes
                // one find takes a fifth of a second. Cleared or Enter: at once.
                onTextChanged: {
                    if (text.length === 0)
                        view.applyFind()
                    else
                        findLater.restart()
                }
                onAccepted: view.applyFind()
                Keys.onEscapePressed: { text = ""; tree.forceActiveFocus() }
            }
            Label {
                id: matches
                objectName: "valuesMatches"
                // Find stopped at a bound: the bound in words on hover.
                property bool limited: false
                color: Theme.textSecondary
                font.pointSize: Type.caption
                HoverHandler { id: matchesHover }
                ToolTip.visible: matches.limited && matchesHover.hovered
                ToolTip.text: view.decoded.findLimitText()
            }
            Label {
                objectName: "valuesCopyNote"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                color: Theme.warning
                font.pointSize: Type.caption
                text: view.copyNote
                Accessible.role: Accessible.AlertMessage
            }
        }

        // The columns' titles, at the rows' widths.
        RowLayout {
            visible: view.decoded.valid
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.padding
            Label { Layout.fillWidth: true; text: qsTr("Name"); font.pointSize: Type.caption; color: Theme.textSecondary }
            Label {
                objectName: "valuesValueHeading"
                Layout.preferredWidth: 10 + view.valueWidth
                leftPadding: Type.groupSpacing
                // Over its numbers, which stand right.
                horizontalAlignment: Text.AlignRight
                text: view.decoded.held ? qsTr("Value · held") : qsTr("Value")
                font.pointSize: Type.caption
                color: view.decoded.held ? Theme.info : Theme.textSecondary
            }
            Item { Layout.preferredWidth: 8 }
            Label {
                visible: view.typeShown
                Layout.preferredWidth: view.typeWidth
                text: qsTr("Type")
                font.pointSize: Type.caption
                color: Theme.textSecondary
            }
            Label {
                visible: view.accessWidth > 0
                Layout.preferredWidth: view.accessWidth
                text: qsTr("rw")
                font.family: Type.fixedFamily
                font.pointSize: Type.caption
                color: Theme.textSecondary
                Accessible.name: qsTr("Access")
            }
        }

        TreeView {
            id: tree
            // A zoom changes the row height: the rows already laid out follow it.
            Connections {
                target: Type
                function onRowHeightChanged() { Qt.callLater(function() { if (tree) tree.forceLayout() }) }
            }
            objectName: "valuesTree"
            visible: view.decoded.valid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: view.decoded.fields
            activeFocusOnTab: true
            focus: true
            selectionModel: ItemSelectionModel { model: tree.model }
            columnWidthProvider: function() { return Math.max(1, tree.width) }
            onWidthChanged: Qt.callLater(function() { if (tree) tree.forceLayout() })
            Keys.onPressed: function(event) {
                const control = event.modifiers & Qt.ControlModifier
                const shift = event.modifiers & Qt.ShiftModifier
                const alt = event.modifiers & Qt.AltModifier
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && shift)) {
                    const item = tree.itemAtCell(Qt.point(0, tree.currentRow))
                    view.openMenu(view.current(), item ? item : tree, 16, item ? item.height : 16)
                } else if (control && shift && event.key === Qt.Key_C) {
                    view.copyAs("text", false)
                } else if (alt && shift && event.key === Qt.Key_C) {
                    view.copyPath()
                } else if (control && event.key === Qt.Key_C) {
                    view.copyValue()
                } else if (control && event.key === Qt.Key_F) {
                    findField.forceActiveFocus()
                } else if (!control && !alt && event.key === Qt.Key_H) {
                    view.toggleHold()
                } else {
                    return
                }
                event.accepted = true
            }
            Keys.onRightPressed: function(event) {
                event.accepted = tree.currentRow >= 0 && !tree.isExpanded(tree.currentRow)
                if (event.accepted)
                    tree.expand(tree.currentRow)
            }
            Keys.onLeftPressed: function(event) {
                event.accepted = tree.currentRow >= 0 && tree.isExpanded(tree.currentRow)
                if (event.accepted)
                    tree.collapse(tree.currentRow)
            }
            delegate: ValuesRow {
                valueWidth: view.valueWidth
                typeWidth: view.typeWidth
                typeShown: view.typeShown
                accessWidth: view.accessWidth
                access: typeof model.access === "string" ? model.access : ""
                onContextMenuRequested: function(index, x, y) { view.openMenu(index, this, x, y) }
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
