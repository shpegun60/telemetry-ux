// The JSON codec's view: what the file holds at its top ("object, 12 keys"),
// and every value as a tree - name, value, type - in the file's own order.
// The tree opens with the top level only; an object's members and an array's
// elements are made when their row is opened, a long one as ranges of 256 -
// [0 … 255], [256 … 511], … - as Eclipse shows a long array. Find narrows the tree to what matches by name, path or
// value. Copy takes a value, a path, or a value exactly as the file writes it.
// MIT.
//
// FileViewer loads it with `decoded`, a JsonContent. An index is taken from
// the selection when it is used, never kept.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

FocusScope {
    id: view
    objectName: "jsonView"

    required property var decoded
    property var content: null

    readonly property real valueWidth: Math.min(320, Math.max(140, width * 0.35))
    readonly property real typeWidth: Math.min(180, Math.max(100, width * 0.2))
    readonly property bool typeShown: width >= 520
    readonly property bool hasCurrent: tree.currentRow >= 0
    property alias findText: findField.text

    function focusContent() { tree.forceActiveFocus() }
    function current() { return tree.selectionModel.currentIndex }
    // Find what the field holds now: the matches counted, the tree shown to them.
    function applyFind() {
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
    Timer {
        id: findLater
        interval: 200
        onTriggered: view.applyFind()
    }
    // A timer of the view's own, not Qt.callLater: closing the tab takes it along.
    Timer {
        id: showLater
        interval: 0
        onTriggered: view.showFound()
    }
    // A value alone; an object or an array as the file writes it.
    function copyValue() {
        const value = view.decoded.valueOf(view.current())
        Clipboard.setText(value.length > 0 ? value : view.decoded.jsonOf(view.current()))
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
    // After Find: the matches' parents open, the matches folded. Cleared:
    // folded again, as it opened, with the current row in view.
    function showFound() {
        if (!tree)
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

    Menu {
        id: nodeMenu
        objectName: "jsonMenu"
        MenuItem {
            objectName: "jsonCopyValue"
            text: qsTr("&Copy value\tCtrl+C")
            enabled: view.hasCurrent
            onTriggered: view.copyValue()
        }
        MenuItem {
            objectName: "jsonCopyPath"
            text: qsTr("Copy &path\tAlt+Shift+C")
            enabled: view.hasCurrent
            onTriggered: view.copyPath()
        }
        MenuItem {
            objectName: "jsonCopyJson"
            text: qsTr("Copy as &JSON\tCtrl+Shift+C")
            enabled: view.hasCurrent
            onTriggered: Clipboard.setText(view.decoded.jsonOf(view.current()))
        }
        MenuItem {
            objectName: "jsonCopyRow"
            text: qsTr("Copy &row")
            enabled: view.hasCurrent
            onTriggered: Clipboard.setText(view.decoded.rowOf(view.current()))
        }
        MenuSeparator {}
        MenuItem {
            objectName: "jsonCopyAll"
            text: qsTr("Copy the &whole file")
            onTriggered: Clipboard.setText(view.decoded.jsonOfAll())
        }
        onClosed: tree.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Type.tight

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.padding
            Label {
                text: qsTr("JSON")
                color: Theme.textPrimary
                font.weight: Type.headingWeight
            }
            Label {
                objectName: "jsonSummary"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: view.decoded.summary
            }
        }

        // Find: the matches and their parents, the rest folded away.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.padding
            TextField {
                id: findField
                objectName: "jsonFind"
                Layout.fillWidth: true
                Layout.maximumWidth: 320
                placeholderText: qsTr("Find by name, path or value (Ctrl+F)")
                Accessible.name: qsTr("Find by name, path or value")
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
                objectName: "jsonMatches"
                property bool limited: false
                color: Theme.textSecondary
                font.pointSize: Type.caption
                HoverHandler { id: matchesHover }
                ToolTip.visible: matches.limited && matchesHover.hovered
                ToolTip.text: view.decoded.findLimitText()
            }
            Item { Layout.fillWidth: true }
        }

        // The columns' titles, at the rows' widths.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.padding
            Label { Layout.fillWidth: true; text: qsTr("Name"); font.pointSize: Type.caption; color: Theme.textSecondary }
            Label {
                Layout.preferredWidth: 10 + view.valueWidth
                leftPadding: Type.groupSpacing
                text: qsTr("Value")
                font.pointSize: Type.caption
                color: Theme.textSecondary
            }
            Item { Layout.preferredWidth: 8 }
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
            objectName: "jsonTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: view.decoded.nodes
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
                    Clipboard.setText(view.decoded.jsonOf(view.current()))
                } else if (alt && shift && event.key === Qt.Key_C) {
                    view.copyPath()
                } else if (control && event.key === Qt.Key_C) {
                    view.copyValue()
                } else if (control && event.key === Qt.Key_F) {
                    findField.forceActiveFocus()
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
            // The Values tree's row: name, value, type; a range like any row.
            delegate: ValuesRow {
                valueWidth: view.valueWidth
                typeWidth: view.typeWidth
                typeShown: view.typeShown
                onContextMenuRequested: function(index, x, y) { view.openMenu(index, this, x, y) }
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
