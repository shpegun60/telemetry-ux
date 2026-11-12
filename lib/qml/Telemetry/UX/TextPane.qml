// The Viewer's Text mode: numbered lines in the fixed font, wrapped or not,
// Find with every match marked, Go to line, and Copy. Only the lines on screen
// are made, so a log of 10 MB reads like a short one. Read-only. MIT.
//
// The host gives `content`, a FileContent; its `lines` is the model. Keys:
// Ctrl+F find (Enter / F3 next, Shift+Enter / Shift+F3 previous), Ctrl+G go
// to line, Ctrl+C copy (the selected text, else the selected lines, else the
// line), Ctrl+A select every line, Alt+Z wrap, Menu / Shift+F10 the menu.
// A click on a line number selects the line, Shift+click up to it.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

FocusScope {
    id: pane
    objectName: "textPane"

    required property var content
    readonly property var lines: content ? content.lines : null
    property bool wrap: false
    // Selected lines, by a click on their numbers: from anchorLine to lastLine.
    property int anchorLine: -1
    property int lastLine: -1
    readonly property int firstSelected: Math.min(anchorLine, lastLine)
    readonly property int lastSelected: Math.max(anchorLine, lastLine)
    // The line's text field that has the keyboard, for its own selection.
    property var editor: null
    readonly property int digits: Math.max(3, String(lines ? lines.lineCount : 0).length)
    readonly property real charWidth: metrics.averageCharacterWidth
    readonly property real gutterWidth: metrics.advanceWidth("0") * digits + 20

    function focusContent() { list.forceActiveFocus() }
    function openFind() { findBar.open() }
    function closeFind() { findBar.close() }
    function selectLines(from, to) {
        // Lines chosen: a selection inside one line no longer counts.
        if (editor)
            editor.deselect()
        editor = null
        anchorLine = from
        lastLine = to
    }
    // The text selected in a line, else the selected lines, else the line.
    function copy() {
        if (editor && editor.selectedText.length > 0)
            Clipboard.setText(editor.selectedText)
        else if (anchorLine >= 0)
            Clipboard.setText(lines.linesText(firstSelected, lastSelected))
        else if (list.currentIndex >= 0)
            Clipboard.setText(lines.lineText(list.currentIndex))
    }
    function showLine(line) {
        if (line < 0)
            return
        list.currentIndex = line
        list.positionViewAtIndex(line, ListView.Contain)
    }
    function findNext(forward) {
        if (findBar.text.length === 0) {
            lines.clearFind()
            findBar.message = ""
            return
        }
        findBar.message = lines.find(findBar.text, findBar.matchCase, forward, Math.max(0, list.currentIndex))
        showLine(lines.matchLine)
    }
    function openGoto() {
        gotoBar.visible = true
        gotoProblem.text = ""
        Qt.callLater(function() {
            gotoField.forceActiveFocus()
            gotoField.selectAll()
        })
    }
    function closeGoto() {
        gotoBar.visible = false
        list.forceActiveFocus()
    }

    FontMetrics {
        id: metrics
        font.family: Type.fixedFamily
    }

    Shortcut {
        sequence: "Ctrl+F"
        // Only while the text has the keyboard: elsewhere these keys are others'.
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
        sequence: "Ctrl+G"
        enabled: pane.visible && pane.activeFocus
        onActivated: pane.openGoto()
    }
    Shortcut {
        sequence: "Ctrl+C"
        enabled: pane.visible && pane.activeFocus && !findBar.field.activeFocus && !gotoField.activeFocus
        onActivated: pane.copy()
    }
    Shortcut {
        sequence: "Ctrl+A"
        enabled: pane.visible && pane.activeFocus && !findBar.field.activeFocus && !gotoField.activeFocus
        onActivated: pane.selectLines(0, pane.lines.lineCount - 1)
    }
    Shortcut {
        sequence: "Alt+Z"
        enabled: pane.visible && pane.activeFocus
        onActivated: pane.wrap = !pane.wrap
    }

    Menu {
        id: contextMenu
        objectName: "textMenu"
        MenuItem {
            text: qsTr("&Copy\tCtrl+C")
            onTriggered: pane.copy()
        }
        MenuItem {
            text: qsTr("Copy &line")
            enabled: list.currentIndex >= 0
            onTriggered: Clipboard.setText(pane.lines.lineText(list.currentIndex))
        }
        MenuItem {
            text: qsTr("Copy &all")
            onTriggered: Clipboard.setText(pane.lines.linesText(0, pane.lines.lineCount - 1))
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("&Find…\tCtrl+F")
            onTriggered: findBar.open()
        }
        MenuItem {
            text: qsTr("&Go to line…\tCtrl+G")
            onTriggered: pane.openGoto()
        }
        MenuItem {
            text: qsTr("Select a&ll\tCtrl+A")
            onTriggered: pane.selectLines(0, pane.lines.lineCount - 1)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "textWrap"
            text: qsTr("&Wrap lines\tAlt+Z")
            checkable: true
            checked: pane.wrap
            onTriggered: pane.wrap = checked
        }
        onClosed: list.forceActiveFocus()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        FindBar {
            id: findBar
            objectName: "textFind"
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            Layout.bottomMargin: Type.tight
            caseOption: true
            // Past the cap a match has no number: only how many there are at least.
            countText: !pane.lines || pane.lines.matchCount === 0 ? ""
                     : pane.lines.matchesCapped && pane.lines.matchIndex === 0 ? qsTr("%1+ matches").arg(pane.lines.matchCount)
                     : pane.lines.matchesCapped ? qsTr("%1 of %2+").arg(pane.lines.matchIndex).arg(pane.lines.matchCount)
                     : qsTr("%1 of %2").arg(pane.lines.matchIndex).arg(pane.lines.matchCount)
            onFindRequested: function(forward) { pane.findNext(forward) }
            onClosed: {
                pane.lines.clearFind()
                list.forceActiveFocus()
            }
        }

        // Go to line (Ctrl+G).
        RowLayout {
            id: gotoBar
            objectName: "textGotoBar"
            visible: false
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            Layout.bottomMargin: Type.tight
            spacing: Type.padding
            Label {
                text: qsTr("Go to line")
                color: Theme.textSecondary
            }
            TextField {
                id: gotoField
                objectName: "textGotoField"
                Layout.preferredWidth: 160
                font.family: Type.fixedFamily
                placeholderText: qsTr("120, or end")
                Accessible.name: qsTr("Line to go to")
                onAccepted: {
                    const line = pane.lines.lineFor(text)
                    if (line < 0) {
                        gotoProblem.text = qsTr("No line %1: the text has %2").arg(text)
                            .arg(pane.lines.lineCount === 1 ? qsTr("1 line") : qsTr("%1 lines").arg(pane.lines.lineCount))
                        return
                    }
                    pane.showLine(line)
                    pane.closeGoto()
                }
                Keys.onEscapePressed: pane.closeGoto()
            }
            Label {
                id: gotoProblem
                objectName: "textGotoProblem"
                Layout.fillWidth: true
                color: Theme.error
                elide: Text.ElideRight
                Accessible.role: Accessible.AlertMessage
            }
            ToolButton {
                flat: true
                implicitWidth: Type.rowHeight
                implicitHeight: Type.rowHeight
                text: qsTr("×")
                Accessible.name: qsTr("Close Go to line")
                onClicked: pane.closeGoto()
            }
        }

        ListView {
            id: list
            objectName: "textLines"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            activeFocusOnTab: true
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            // Only while the text is shown: a hidden tab keeps no rows.
            model: pane.visible ? pane.lines : null
            // Unwrapped, as wide as the longest line; wrapped, as wide as the view.
            contentWidth: pane.wrap ? width : Math.max(width, pane.gutterWidth + pane.charWidth * (pane.lines ? pane.lines.longestLine : 0) + 24)
            flickableDirection: pane.wrap ? Flickable.VerticalFlick : Flickable.HorizontalAndVerticalFlick
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Text, %1").arg(pane.lines && pane.lines.lineCount === 1 ? qsTr("1 line")
                                                                                       : qsTr("%1 lines").arg(pane.lines ? pane.lines.lineCount : 0))
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    contextMenu.popup(list, 16, 16)
                    event.accepted = true
                } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Home) {
                    pane.showLine(0)
                    event.accepted = true
                } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_End) {
                    pane.showLine(pane.lines.lineCount - 1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape && pane.anchorLine >= 0) {
                    pane.selectLines(-1, -1)
                    event.accepted = true
                }
            }
            // Scroll bars whenever there is more than shows, as Hex has them.
            ScrollBar.vertical: ScrollBar {
                policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            ScrollBar.horizontal: ScrollBar {
                policy: !pane.wrap && list.contentWidth > list.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

            delegate: Item {
                id: lineItem
                required property int index
                required property int number
                required property string line
                required property var matches
                required property int currentMatch
                required property int hidden
                readonly property bool selected: pane.anchorLine >= 0 && index >= pane.firstSelected && index <= pane.lastSelected
                readonly property bool current: ListView.isCurrentItem

                width: list.contentWidth
                height: Math.max(metrics.height + 6, lineText.contentHeight + 6)
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("Line %1: %2").arg(number).arg(line)
                ListView.onReused: if (pane.editor === lineText) pane.editor = null

                Rectangle {
                    anchors.fill: parent
                    color: lineItem.selected ? Theme.stateSelected : lineItem.current ? Theme.stateHover : "transparent"
                }
                // The line's number: click to select the line, Shift+click to select up to it.
                Label {
                    id: gutter
                    width: pane.gutterWidth - 12
                    height: parent.height
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignTop
                    topPadding: 3
                    font.family: Type.fixedFamily
                    color: lineItem.selected ? Theme.selectedForeground : Theme.textMuted
                    text: lineItem.number
                    Accessible.ignored: true
                    TapHandler {
                        onTapped: function(point) {
                            list.currentIndex = lineItem.index
                            if ((point.modifiers & Qt.ShiftModifier) && pane.anchorLine >= 0)
                                pane.lastLine = lineItem.index
                            else
                                pane.selectLines(lineItem.index, lineItem.index)
                            list.forceActiveFocus()
                        }
                    }
                }
                // Find's matches under the text, the current one stronger.
                Repeater {
                    model: lineItem.matches
                    delegate: Rectangle {
                        required property var modelData
                        // Read again whenever the line is laid out again (wrap, width).
                        readonly property rect from: (lineText.width, lineText.contentHeight,
                                                      lineText.positionToRectangle(modelData[0]))
                        readonly property rect to: (lineText.width, lineText.contentHeight,
                                                    lineText.positionToRectangle(modelData[0] + modelData[1]))
                        x: lineText.x + from.x
                        y: lineText.y + from.y
                        width: Math.max(2, to.y === from.y ? to.x - from.x : lineText.width - from.x)
                        height: from.height
                        radius: 2
                        color: modelData[0] === lineItem.currentMatch ? Theme.selectionFill : Theme.infoContainer
                        border.width: modelData[0] === lineItem.currentMatch ? 0 : 1
                        border.color: Theme.info
                    }
                }
                TextEdit {
                    id: lineText
                    x: pane.gutterWidth
                    y: 3
                    width: pane.wrap ? lineItem.width - pane.gutterWidth - 8 : implicitWidth
                    readOnly: true
                    selectByMouse: true
                    persistentSelection: false
                    textFormat: TextEdit.PlainText
                    wrapMode: pane.wrap ? TextEdit.WrapAnywhere : TextEdit.NoWrap
                    font.family: Type.fixedFamily
                    // A tab to the next multiple of eight columns, as the width is counted.
                    tabStopDistance: pane.charWidth * 8
                    color: Theme.textPrimary
                    selectionColor: Theme.selectionFill
                    selectedTextColor: Theme.selectedForeground
                    text: lineItem.line
                    // The selection Copy takes stays when a menu takes the keyboard;
                    // it goes when it is emptied or another line is chosen.
                    onActiveFocusChanged: {
                        if (activeFocus) {
                            if (pane.editor && pane.editor !== lineText)
                                pane.editor.deselect()
                            pane.editor = lineText
                            pane.anchorLine = -1
                            pane.lastLine = -1
                            list.currentIndex = lineItem.index
                        }
                    }
                    onSelectedTextChanged: if (selectedText.length === 0 && pane.editor === lineText && !activeFocus) pane.editor = null
                }
                // A line longer than is shown says how much more it has; Copy takes it whole.
                Label {
                    visible: lineItem.hidden > 0
                    x: lineText.x + lineText.width + 8
                    y: lineText.y
                    font.pointSize: Type.caption
                    color: Theme.textMuted
                    text: visible ? qsTr("… %1 more characters").arg(lineItem.hidden) : ""
                }
                // The current match's characters over its fill, in the selected
                // foreground: the line's own colour is too faint on the selection.
                Text {
                    readonly property int length: {
                        for (let i = 0; i < lineItem.matches.length; ++i)
                            if (lineItem.matches[i][0] === lineItem.currentMatch)
                                return lineItem.matches[i][1]
                        return 0
                    }
                    readonly property rect at: (lineText.width, lineText.contentHeight,
                                                lineItem.currentMatch >= 0 ? lineText.positionToRectangle(lineItem.currentMatch)
                                                                           : Qt.rect(0, 0, 0, 0))
                    visible: lineItem.currentMatch >= 0 && length > 0
                    x: lineText.x + at.x
                    y: lineText.y + at.y
                    textFormat: Text.PlainText
                    font: lineText.font
                    color: Theme.selectedForeground
                    text: visible ? lineItem.line.substr(lineItem.currentMatch, length) : ""
                    Accessible.ignored: true
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) {
                        list.currentIndex = lineItem.index
                        contextMenu.popup(lineItem, point.position.x, point.position.y)
                    }
                }
            }
        }
    }
}
