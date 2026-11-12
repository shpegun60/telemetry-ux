// The values codec's view: which descriptor the file was written with (its
// fingerprint, with Copy), what it holds, and every Field as a tree with its
// value - group, Field, a struct's members, an array's elements. Live reads
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
    // Null for a file from disk: then there is nothing to read again.
    readonly property var live: content ? content.live : null
    readonly property bool running: !!live && live.running

    readonly property real valueWidth: Math.min(260, Math.max(140, width * 0.3))
    readonly property real typeWidth: Math.min(180, Math.max(100, width * 0.2))
    readonly property bool typeShown: width >= 520
    // For enabling the menu only; actions read current() when they run.
    readonly property bool hasCurrent: tree.currentRow >= 0
    property alias findText: findField.text

    // Read again and again only while shown: a hidden view reads nothing.
    Component.onDestruction: if (live) live.running = false

    function focusContent() { tree.forceActiveFocus() }
    function current() { return tree.selectionModel.currentIndex }
    function copyAs(format, whole) {
        const text = whole ? view.decoded.textOfAll(format) : view.decoded.textOf(view.current(), format)
        if (text.length > 0)
            Clipboard.setText(text)
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
    function openMenu(index, from, x, y) {
        if (index && index.valid)
            tree.selectionModel.setCurrentIndex(index, ItemSelectionModel.NoUpdate)
        nodeMenu.popup(from, x, y)
    }
    // Groups open, Fields folded: every Field's value at a glance. Called
    // later, so the view may have gone meanwhile (its tab closed).
    function firstLook() {
        if (!tree)
            return
        for (let row = tree.rows - 1; row >= 0; --row)
            if (tree.depth(row) === 0)
                tree.expand(row)
    }
    function timeText(time) {
        return isNaN(time) ? "" : Qt.formatTime(time, "HH:mm:ss.zzz")
    }
    // After Find: the matches' parents open, the matches folded. Cleared: the
    // first look again - groups open - with the current row in view.
    function showFound() {
        if (!tree)
            return
        if (view.findText.length === 0) {
            tree.collapseRecursively()
            for (let row = tree.rows - 1; row >= 0; --row)
                if (tree.depth(row) === 0)
                    tree.expand(row)
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
        function onSnapshot(bytes) {
            const problem = view.decoded.update(bytes)
            if (problem.length > 0) {
                view.live.running = false
                view.stoppedBecause = qsTr("Live stopped: the device's file is %1").arg(problem)
            }
        }
        function onStateChanged() { if (view.live.running) view.stoppedBecause = "" }
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
            onTriggered: Clipboard.setText(view.decoded.pathOf(view.current()))
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
        onClosed: tree.forceActiveFocus()
    }

    FileDialog {
        id: descriptorDialog
        title: qsTr("Choose the descriptor this file was written with")
        nameFilters: [qsTr("Descriptor (*.bin)"), qsTr("All files (*)")]
        onAccepted: descriptorProblem.text = view.decoded.useDescriptor(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        // Which descriptor the file was written with: its fingerprint, with Copy.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            Label {
                text: qsTr("Values")
                color: Theme.textPrimary
                font.weight: Type.headingWeight
            }
            Label {
                text: qsTr("descriptor")
                font.pointSize: Type.caption
                color: Theme.textSecondary
            }
            Label {
                objectName: "valuesFingerprint"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.family: Type.fixedFamily
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
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textPrimary
                text: qsTr("These values can be read only with the descriptor they were written with, and it is not at hand.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                text: qsTr("Connect to the device, or choose its descriptor.bin - one downloaded with this file has the same fingerprint.")
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

        Label {
            objectName: "valuesSummary"
            visible: view.decoded.valid
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            elide: Text.ElideRight
            font.pointSize: Type.caption
            color: Theme.textSecondary
            text: view.live ? view.decoded.summary
                            : qsTr("%1 · read %2").arg(view.decoded.summary).arg(view.timeText(view.decoded.shownAt))
        }

        // Live: read again and again; Hold: keep what is shown.
        RowLayout {
            objectName: "valuesLiveBar"
            visible: view.decoded.valid && !!view.live
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            // Shown pressed while on; the state is the reader's, never the button's own.
            Button {
                objectName: "valuesLive"
                text: qsTr("Live")
                checked: view.running
                Accessible.checkable: true
                Accessible.checked: view.running
                onClicked: view.live.running = !view.running
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Read the device's file again and again")
            }
            ComboBox {
                id: period
                objectName: "valuesPeriod"
                Accessible.name: qsTr("How often to read")
                textRole: "text"
                valueRole: "ms"
                // The fastest reads as soon as the last read ended (200 ms at
                // least between starts); the others wait that long after it.
                model: [
                    { text: qsTr("As fast as it reads"), ms: 200 },
                    { text: qsTr("Every 0.5 s"), ms: 500 },
                    { text: qsTr("Every 1 s"), ms: 1000 },
                    { text: qsTr("Every 2 s"), ms: 2000 },
                    { text: qsTr("Every 5 s"), ms: 5000 },
                    { text: qsTr("Every 10 s"), ms: 10000 }
                ]
                Component.onCompleted: currentIndex = Math.max(0, indexOfValue(view.live ? view.live.periodMs : 1000))
                onActivated: if (view.live) view.live.periodMs = currentValue
            }
            Button {
                objectName: "valuesHold"
                text: qsTr("Hold")
                checked: view.decoded.held
                Accessible.checkable: true
                Accessible.checked: view.decoded.held
                enabled: view.running || view.decoded.held
                onClicked: view.toggleHold()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Keep these values on screen while reading goes on (H)")
            }
            Label {
                objectName: "valuesLiveState"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: view.stoppedBecause.length > 0 || (view.live && view.live.problem.length > 0) ? Theme.warning
                                                                                                       : Theme.textSecondary
                text: {
                    const shown = view.timeText(view.decoded.shownAt)
                    if (!view.live)
                        return ""
                    if (view.stoppedBecause.length > 0)
                        return view.stoppedBecause
                    if (view.live.problem.length > 0)
                        return view.live.problem
                    if (view.decoded.held)
                        return view.decoded.waiting === 0 ? qsTr("Held at %1").arg(shown)
                             : view.decoded.waiting === 1 ? qsTr("Held at %1 · 1 newer read").arg(shown)
                             : qsTr("Held at %1 · %2 newer reads").arg(shown).arg(view.decoded.waiting)
                    const changed = view.decoded.changedCount === 1 ? qsTr("1 value changed")
                                  : qsTr("%1 values changed").arg(view.decoded.changedCount)
                    return view.running ? qsTr("Read at %1 · %2 · the read took %3 ms").arg(shown).arg(changed).arg(view.live.lastReadMs)
                                        : qsTr("Read at %1").arg(shown)
                }
            }
        }

        // Find: the matches and their parents, the rest folded away.
        RowLayout {
            visible: view.decoded.valid
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            TextField {
                id: findField
                objectName: "valuesFind"
                Layout.fillWidth: true
                Layout.maximumWidth: 320
                placeholderText: qsTr("Find by name or path (Ctrl+F)")
                Accessible.name: qsTr("Find by name or path")
                onTextChanged: {
                    const found = view.decoded.setFilter(text)
                    matches.text = text.length === 0 ? "" : found === 1 ? qsTr("1 match") : qsTr("%1 matches").arg(found)
                    Qt.callLater(view.showFound)
                }
                Keys.onEscapePressed: { text = ""; tree.forceActiveFocus() }
            }
            Label {
                id: matches
                objectName: "valuesMatches"
                color: Theme.textSecondary
                font.pointSize: Type.caption
            }
            Item { Layout.fillWidth: true }
        }

        // The columns' titles, at the rows' widths.
        RowLayout {
            visible: view.decoded.valid
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8
            Label { Layout.fillWidth: true; text: qsTr("Name"); font.pointSize: Type.caption; color: Theme.textSecondary }
            Item { Layout.preferredWidth: 6 }
            Label { Layout.preferredWidth: view.valueWidth; text: qsTr("Value"); font.pointSize: Type.caption; color: Theme.textSecondary }
            Label {
                visible: view.typeShown
                Layout.preferredWidth: view.typeWidth
                text: qsTr("Type")
                font.pointSize: Type.caption
                color: Theme.textSecondary
            }
        }

        TreeView {
            id: tree
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
                    Clipboard.setText(view.decoded.pathOf(view.current()))
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
                onContextMenuRequested: function(index, x, y) { view.openMenu(index, this, x, y) }
            }
            ScrollBar.vertical: ScrollBar {}
            // The first rows - now, or once a chosen descriptor fills the tree - get the first look.
            property bool expandedOnce: false
            onRowsChanged: if (rows > 0 && !expandedOnce) { expandedOnce = true; Qt.callLater(view.firstLook) }
            Component.onCompleted: if (rows > 0 && !expandedOnce) { expandedOnce = true; Qt.callLater(view.firstLook) }
        }
    }
}
