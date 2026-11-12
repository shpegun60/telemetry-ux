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
    // The text the last search was for: typing a new one waits, Enter on it does not.
    property string lastFound: ""

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
    // A pause in typing before a large table is filtered or searched.
    Timer {
        id: filterLater
        interval: 200
        onTriggered: pane.rows.setFilter(filterField.text)
    }
    Timer {
        id: findLater
        interval: 200
        onTriggered: pane.findNext(true)
    }
    function findNext(forward) {
        if (findBar.text.length === 0) {
            rows.clearFind()
            findBar.message = ""
            return
        }
        const at = current()
        findLater.stop()
        lastFound = findBar.text
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
        MenuSeparator {}
        MenuItem {
            objectName: "tableSortAscending"
            readonly property int column: pane.current().valid ? pane.current().column : -1
            text: column < 0 ? qsTr("Sort &ascending") : qsTr("Sort &ascending by %1").arg(pane.rows.heading(column))
            enabled: column >= 0
            checkable: true
            checked: column >= 0 && pane.rows.sortedColumn === column && !pane.rows.descending
            onTriggered: pane.rows.sortColumn(column, false)
        }
        MenuItem {
            objectName: "tableSortDescending"
            readonly property int column: pane.current().valid ? pane.current().column : -1
            text: column < 0 ? qsTr("Sort &descending") : qsTr("Sort &descending by %1").arg(pane.rows.heading(column))
            enabled: column >= 0
            checkable: true
            checked: column >= 0 && pane.rows.sortedColumn === column && pane.rows.descending
            onTriggered: pane.rows.sortColumn(column, true)
        }
        MenuItem {
            objectName: "tableClearSort"
            text: qsTr("Clear sort (file &order)")
            enabled: pane.rows && pane.rows.sortedColumn >= 0
            onTriggered: pane.rows.clearSort()
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
            Label {
                text: qsTr("Filter")
                color: Theme.textSecondary
            }
            TextField {
                id: filterField
                objectName: "tableFilter"
                Layout.fillWidth: true
                Layout.maximumWidth: 260
                placeholderText: qsTr("Filter rows (Ctrl+L)")
                Accessible.name: qsTr("Filter rows")
                onTextChanged: filterLater.restart()
                // Enter filters at once.
                onAccepted: {
                    filterLater.stop()
                    pane.rows.setFilter(text)
                }
                Keys.onEscapePressed: {
                    text = ""
                    table.forceActiveFocus()
                }
            }
            ToolButton {
                objectName: "tableFilterClear"
                visible: filterField.text.length > 0
                flat: true
                implicitWidth: 24
                implicitHeight: 24
                text: qsTr("×")
                Accessible.name: qsTr("Clear filter")
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: qsTr("Clear filter (Esc)")
                onClicked: {
                    filterField.text = ""
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
                     : pane.rows.matchesCapped ? qsTr("%1 of %2+").arg(pane.rows.matchIndex).arg(pane.rows.matchCount)
                     : qsTr("%1 of %2").arg(pane.rows.matchIndex).arg(pane.rows.matchCount)
            // Typing waits for a pause; Enter, F3, Next and Previous go at once.
            onFindRequested: function(forward) {
                if (forward && findBar.field.activeFocus && findBar.text !== pane.lastFound)
                    findLater.restart()
                else
                    pane.findNext(forward)
            }
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
                readonly property bool numeric: pane.rows && pane.rows.numeric(column)
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
                    anchors.leftMargin: heading.numeric ? 20 : 8
                    anchors.rightMargin: heading.numeric ? 8 : 20
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: heading.numeric ? Text.AlignRight : Text.AlignLeft
                    elide: Text.ElideRight
                    font.weight: Type.headingWeight
                    color: Theme.textPrimary
                    text: heading.display
                }
                Label {
                    x: heading.numeric ? 6 : parent.width - width - 6
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
                // What the next click does.
                ToolTip.text: !heading.sorted ? qsTr("Sort ascending by %1").arg(heading.display)
                            : !pane.rows.descending ? qsTr("Sort descending by %1").arg(heading.display)
                            : qsTr("Back to file order")
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
            // Only while the table is shown: a hidden tab keeps no cells.
            model: pane.visible ? pane.rows : null
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
                border.width: current ? 2 : match ? 1 : 0
                border.color: current ? (table.activeFocus ? Theme.focusRing : Theme.outline) : Theme.info
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
            ScrollBar.vertical: ScrollBar {
                policy: table.contentHeight > table.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            ScrollBar.horizontal: ScrollBar {
                policy: table.contentWidth > table.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
        }
    }
}
