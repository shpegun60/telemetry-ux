// The Viewer's Table mode: a CSV's rows under its headings. A heading sorts
// its column - ascending, descending, then the file's order - numbers as
// numbers and words in natural order; Filter keeps the rows containing a
// text; Find walks the shown cells. Read-only. MIT.
//
// The host gives `content`, a FileContent; its `rows` is the model. Keys:
// Ctrl+F find (Enter / F3 next, Shift+Enter / Shift+F3 previous), Ctrl+L
// filter, Ctrl+C copy the cell, Menu / Shift+F10 the menu.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

FocusScope {
    id: pane
    objectName: "tablePane"

    required property var content
    readonly property var rows: content ? content.rows : null
    readonly property alias view: table

    function focusContent() { table.forceActiveFocus() }
    // A heading is never cut: a column is as wide as its widest cell or its heading.
    FontMetrics {
        id: headingMetrics
        font.weight: Type.headingWeight
    }
    function current() { return table.selectionModel.currentIndex }
    function showCell(row, column) {
        if (row < 0)
            return
        table.selectionModel.setCurrentIndex(rows.index(row, column), ItemSelectionModel.NoUpdate)
        table.positionViewAtCell(Qt.point(column, row), TableView.Contain)
    }
    function findNext(forward) {
        if (findBar.text.length === 0) {
            rows.clearFind()
            findBar.message = ""
            return
        }
        const at = current()
        const found = rows.find(findBar.text, forward, at.valid ? at.row : 0, at.valid ? at.column : 0)
        findBar.message = found.message
        showCell(found.row, found.column)
    }
    function copyCell() {
        const at = current()
        if (at.valid)
            Clipboard.setText(rows.cellText(at.row, at.column))
    }

    Shortcut {
        sequence: "Ctrl+F"
        // Only while the table has the keyboard: elsewhere these keys are others'.
        enabled: pane.visible && pane.activeFocus
        onActivated: findBar.open()
    }
    Shortcut {
        sequence: "F3"
        enabled: pane.visible && pane.activeFocus && findBar.text.length > 0
        onActivated: pane.findNext(true)
    }
    Shortcut {
        sequence: "Shift+F3"
        enabled: pane.visible && pane.activeFocus && findBar.text.length > 0
        onActivated: pane.findNext(false)
    }
    Shortcut {
        sequence: "Ctrl+L"
        enabled: pane.visible && pane.activeFocus
        onActivated: {
            filterField.forceActiveFocus()
            filterField.selectAll()
        }
    }
    Shortcut {
        sequence: "Ctrl+C"
        enabled: pane.visible && table.activeFocus
        onActivated: pane.copyCell()
    }

    Menu {
        id: contextMenu
        objectName: "tableMenu"
        MenuItem {
            text: qsTr("&Copy cell\tCtrl+C")
            enabled: pane.current().valid
            onTriggered: pane.copyCell()
        }
        MenuItem {
            text: qsTr("Copy &row")
            enabled: pane.current().valid
            onTriggered: Clipboard.setText(pane.rows.rowText(pane.current().row, "tsv"))
        }
        Menu {
            title: filterField.text.length > 0 ? qsTr("Copy shown rows a&s") : qsTr("Copy a&ll as")
            MenuItem { text: qsTr("&CSV"); onTriggered: Clipboard.setText(pane.rows.shownText("csv")) }
            MenuItem { text: qsTr("&TSV (for a spreadsheet)"); onTriggered: Clipboard.setText(pane.rows.shownText("tsv")) }
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("&Find…\tCtrl+F")
            onTriggered: findBar.open()
        }
        MenuItem {
            text: qsTr("F&ilter rows…\tCtrl+L")
            onTriggered: {
                filterField.forceActiveFocus()
                filterField.selectAll()
            }
        }
        MenuItem {
            text: qsTr("File's &order")
            enabled: pane.rows && pane.rows.sortedColumn >= 0
            onTriggered: {
                while (pane.rows.sortedColumn >= 0)
                    pane.rows.sortBy(pane.rows.sortedColumn)
            }
        }
        onClosed: table.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Filter: only the rows with this text in some cell.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            spacing: 8
            TextField {
                id: filterField
                objectName: "tableFilter"
                Layout.fillWidth: true
                Layout.maximumWidth: 260
                placeholderText: qsTr("Filter rows (Ctrl+L)")
                Accessible.name: qsTr("Filter rows")
                onTextChanged: pane.rows.setFilter(text)
                Keys.onEscapePressed: {
                    text = ""
                    table.forceActiveFocus()
                }
            }
            Label {
                objectName: "tableShown"
                color: Theme.textSecondary
                font.pointSize: Type.caption
                text: !pane.rows ? ""
                    : pane.rows.shownRows === pane.rows.totalRows
                      ? (pane.rows.totalRows === 1 ? qsTr("1 row") : qsTr("%1 rows").arg(pane.rows.totalRows))
                      : qsTr("%1 of %2 rows").arg(pane.rows.shownRows).arg(pane.rows.totalRows)
            }
            Item { Layout.fillWidth: true }
        }

        FindBar {
            id: findBar
            objectName: "tableFind"
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            countText: !pane.rows || pane.rows.matchCount === 0 ? ""
                     : qsTr("%1 of %2").arg(pane.rows.matchIndex).arg(pane.rows.matchCount)
            onFindRequested: function(forward) { pane.findNext(forward) }
            onClosed: {
                pane.rows.clearFind()
                table.forceActiveFocus()
            }
        }

        // The headings: a click sorts by the column, the arrow says which way.
        HorizontalHeaderView {
            id: header
            objectName: "fileViewerTableHeader"
            Layout.fillWidth: true
            syncView: table
            clip: true
            delegate: Rectangle {
                id: heading
                required property int column
                required property string display
                readonly property bool sorted: pane.rows && pane.rows.sortedColumn === column
                implicitWidth: Math.max(72, title.implicitWidth + 32)
                implicitHeight: Type.rowHeight
                color: hover.hovered ? Theme.stateHover : Theme.surfaceBar
                Accessible.role: Accessible.ColumnHeader
                Accessible.name: !sorted ? display
                               : pane.rows.descending ? qsTr("%1, sorted descending").arg(display)
                               : qsTr("%1, sorted ascending").arg(display)
                Label {
                    id: title
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 20
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: pane.rows && pane.rows.numeric(heading.column) ? Text.AlignRight : Text.AlignLeft
                    elide: Text.ElideRight
                    font.weight: Type.headingWeight
                    color: Theme.textPrimary
                    text: heading.display
                }
                Label {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    visible: heading.sorted
                    color: Theme.accent
                    text: pane.rows && pane.rows.descending ? "▼" : "▲"
                    Accessible.ignored: true
                }
                HoverHandler { id: hover }
                TapHandler {
                    onTapped: pane.rows.sortBy(heading.column)
                }
                ToolTip.visible: hover.hovered
                ToolTip.text: qsTr("Sort by %1").arg(heading.display)
            }
        }

        TableView {
            id: table
            objectName: "fileViewerTable"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            columnSpacing: 1
            rowSpacing: 1
            activeFocusOnTab: true
            focus: true
            keyNavigationEnabled: true
            selectionModel: ItemSelectionModel { model: table.model }
            model: pane.rows
            columnWidthProvider: function(column) {
                const cells = table.implicitColumnWidth(column)
                const heading = pane.rows ? headingMetrics.advanceWidth(pane.rows.heading(column)) + 40 : 0
                return Math.min(420, Math.max(cells, heading, 72))
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    const at = pane.current()
                    const cell = at.valid ? table.itemAtIndex(at) : null
                    contextMenu.popup(cell ? cell : table, 16, cell ? cell.height : 16)
                    event.accepted = true
                }
            }
            delegate: Rectangle {
                id: tableCell
                required property string display
                required property int row
                required property int column
                required property bool current
                // A cell Find would land on, while Find is open.
                readonly property bool match: findBar.visible && findBar.text.length > 0 &&
                                              display.toLowerCase().indexOf(findBar.text.toLowerCase()) >= 0
                implicitWidth: Math.min(360, Math.max(72, cell.implicitWidth + 16))
                implicitHeight: Type.rowHeight
                color: match ? Theme.infoContainer : row % 2 ? Theme.surfaceAlternate : Theme.surface
                border.width: current ? 2 : 0
                border.color: table.activeFocus ? Theme.focusRing : Theme.outline
                Accessible.role: Accessible.Cell
                Accessible.name: qsTr("%1: %2").arg(pane.rows.heading(tableCell.column)).arg(tableCell.display)
                Label {
                    id: cell
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: pane.rows.numeric(tableCell.column) ? Text.AlignRight : Text.AlignLeft
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textPrimary
                    text: tableCell.display
                }
                HoverHandler { id: cellHover }
                ToolTip.visible: cellHover.hovered && cell.truncated
                ToolTip.text: tableCell.display
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) {
                        table.selectionModel.setCurrentIndex(pane.rows.index(tableCell.row, tableCell.column),
                                                             ItemSelectionModel.NoUpdate)
                        contextMenu.popup(tableCell, point.position.x, point.position.y)
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}
        }
    }
}
