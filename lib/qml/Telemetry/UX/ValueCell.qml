// One value in Details' and the Batch's tables: a value until it is opened,
// then edited in place (family spec C). The owner gives it the row's roles and
// the way to write text into the row; edits are live. Pure QML, so a host
// needs none of the module's C++ types for it. MIT.
//
// Keys while open: Enter keeps the value, and so does focus going elsewhere
// (a click away, Tab); Tab and Shift+Tab also ask the owner for the next or
// previous cell (tabbed); Escape puts back the value the cell had when it was
// opened. Closing with text that is no value puts back the last value that
// was, and the caption under the closed cell says why ("Not changed: Enter a
// number from 0 to 255."). Ctrl+Enter keeps the value and goes on to the owner
// (Apply, Execute or Call).
import QtQuick
import QtQuick.Controls
// The editor is drawn here (22 px, a 1 px outline): Basic takes that in every
// host style, where a native style refuses a custom background.
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Shapes

Item {
    id: cell

    // The row's roles.
    property string name: ""
    // The row's name, as a host's check finds a cell by it.
    readonly property string scalarName: name
    property string value: ""
    property string typeText: ""
    property string detail: ""
    property bool isAggregate: false
    property int childCount: 0
    property int loadedCount: 0
    property bool editable: false
    property string editorText: ""
    property string editorKind: "text"
    property var editorChoices: []
    property bool choicesTruncated: false
    property string canonicalText: ""
    // What the cell says while its text is no value: "Enter a number from 0 to 255."
    property string takes: ""
    // Those words, or the type's when the owner gives none.
    readonly property string takesText: takes.length > 0 ? takes : qsTr("Enter a value of type %1.").arg(typeText)
    // How far left of the cell its editor may grow over the owner's spare
    // room (the Name column's), so a long value shows whole while edited; 0
    // keeps the editor in the cell. The right edge stays on the cell's.
    property real editorRoom: 0
    // Codes in hexadecimal beside the names, "Idle (0x00)", as the
    // Watchlist shows them in its hex form; decimal otherwise.
    property bool hexCodes: false
    // The row is selected: its text is on the selection colour.
    property bool highlighted: false
    // From an earlier run (family spec A6): italic and muted, the note in the
    // tooltip ("from the call at 14:02:11; values changed since").
    property bool stale: false
    property string staleNote: ""
    // The value's own width, for an owner whose column grows to its widest.
    readonly property real textWidth: shown.implicitWidth
    // Nothing opens: changes are off, or the owner is busy. The value stays.
    property bool locked: false
    // Writes text into the row; returns whether the text is a value.
    property var setText: null
    // Loads the next 128 members of a large struct or array.
    property var loadMore: null

    // Numbers and true/false stand right, as the Device tree's do.
    readonly property bool numeric: /^(?:[usi](?:8|16|32|64)|f(?:32|64)|u?int(?:8|16|32|64)|float(?:32|64)|bool)$/.test(typeText)
    readonly property bool floating: /^(?:f(?:32|64)|float(?:32|64))$/.test(typeText)
    readonly property bool choices: editorKind === "bool" || editorKind === "enum"
    readonly property bool canOpen: editable && !locked && !isAggregate && typeof setText === "function"
    // The editor is open.
    property bool open: false
    // The text in the editor is no value; the caption says what is.
    property bool invalid: false
    // An enum's code typed rather than chosen.
    property bool manualCode: false
    property string startText: ""
    property bool closing: false
    // Why the last text left was not kept: "Not changed: Enter a number from
    // 0 to 255.", under the closed cell until it is opened again.
    property string refusal: ""

    // Tab or Shift+Tab: the owner opens the next or previous cell.
    signal tabbed(bool forward)
    // The editor closed; byKey when by Enter or Escape, so the owner takes
    // the keyboard back (not when focus went elsewhere).
    signal finished(bool byKey)

    implicitHeight: column.implicitHeight
    implicitWidth: Type.bodyPixels * 8

    // Opens the editor; false when this value cannot be edited now.
    function begin() {
        if (!canOpen)
            return false
        startText = editorText
        refusal = ""
        invalid = false
        manualCode = false
        open = true
        input.text = editorText
        if (choices) {
            choice.forceActiveFocus()
            choice.popup.open()
        } else {
            input.forceActiveFocus()
            input.selectAll()
        }
        return true
    }
    // Closes it, keeping what was typed when it is a value; otherwise the last
    // value comes back.
    function keep(byKey) {
        if (!open || closing)
            return
        closing = true
        // Text that is no value is never dropped in silence: the last value
        // comes back, and the caption says why, in Enter's words.
        if (invalid) {
            refusal = qsTr("Not changed: %1").arg(takesText)
            if (typeof setText === "function")
                setText(editorText)
        }
        invalid = false
        manualCode = false
        open = false
        closing = false
        finished(byKey === true)
    }
    // Closes it and puts back the value it had when it was opened.
    function revert() {
        if (!open || closing)
            return
        closing = true
        refusal = ""
        if (typeof setText === "function" && editorText !== startText)
            setText(startText)
        invalid = false
        manualCode = false
        open = false
        closing = false
        finished(true)
    }
    function reset() {
        refusal = ""
        open = false
        invalid = false
        manualCode = false
    }

    // A choice's words with its code beside the name, so the constant
    // written is in sight: "false (0)", "Idle (0)", "Idle (0x00)" in hex.
    function codeLabel(item) {
        let label = item.label
        if (editorKind === "bool" && !/\(\d+\)$/.test(label))
            label = item.text + (item.text === "true" ? " (1)" : " (0)")
        if (!hexCodes)
            return label
        const found = /^(.*) \((-?\d+)\)$/.exec(label)
        if (!found)
            return label
        const code = Number(found[2])
        const bits = /(?:8|16|32|64)/.exec(typeText)
        const digits = bits ? Number(bits[0]) / 4 : 2
        const shown = code < 0 ? "-0x" + (-code).toString(16).toUpperCase().padStart(digits, "0")
                               : "0x" + code.toString(16).toUpperCase().padStart(digits, "0")
        return found[1] + " (" + shown + ")"
    }
    readonly property var options: {
        const rows = []
        for (let i = 0; i < editorChoices.length; ++i) {
            const item = editorChoices[i]
            if (item && typeof item.label === "string" && typeof item.text === "string")
                rows.push({ label: codeLabel(item), text: item.text, manual: false })
        }
        if (editorKind === "enum" && !rows.some(function(item) { return item.text === cell.editorText }))
            rows.unshift({ label: (choicesTruncated ? qsTr("Code (%1)") : qsTr("Unknown (%1)")).arg(editorText),
                           text: editorText, manual: false })
        if (editorKind === "enum")
            rows.push({ label: qsTr("Enter code…"), text: "", manual: true })
        return rows
    }
    readonly property int optionIndex: {
        for (let i = 0; i < options.length; ++i)
            if (!options[i].manual && options[i].text === editorText)
                return i
        return -1
    }

    ColumnLayout {
        id: column
        width: parent.width
        spacing: Type.tight

        Item {
            id: line
            Layout.fillWidth: true
            implicitHeight: Type.rowHeight

            HoverHandler { id: hover; enabled: cell.canOpen && !cell.open }
            TapHandler {
                enabled: cell.canOpen && !cell.open
                onTapped: cell.begin()
            }
            // A cell that opens on a click says so under the pointer.
            Rectangle {
                anchors.fill: parent
                visible: hover.hovered && !cell.highlighted
                color: Theme.stateHover
                Accessible.ignored: true
            }
            Label {
                id: shown
                objectName: "valueText"
                property string scalarName: cell.name
                visible: !cell.open
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                elide: Text.ElideRight
                // An enum reads its name with the code, as its list does.
                text: cell.editorKind === "enum" && cell.optionIndex >= 0 && !cell.isAggregate
                      ? cell.options[cell.optionIndex].label : cell.value
                color: cell.highlighted ? Theme.selectedForeground
                    : cell.isAggregate || cell.stale ? Theme.textMuted : Theme.textPrimary
                font.family: cell.isAggregate ? Qt.application.font.family : Type.fixedFamily
                font.italic: cell.stale
                horizontalAlignment: cell.numeric ? Text.AlignRight : Text.AlignLeft
                Accessible.description: cell.stale ? qsTr("From an earlier run; the values have changed since.") : ""
                HoverHandler { id: shownHover }
                PlainToolTip {
                    visible: shownHover.hovered && (shown.truncated || (cell.stale && cell.staleNote.length > 0))
                    text: cell.stale && cell.staleNote.length > 0 ? cell.value + " · " + cell.staleNote : cell.value
                }
            }
            // The pencil sits in the gap after the value, so digits never move.
            Shape {
                id: pencil
                objectName: "valueEditGlyph"
                // On a 12-unit grid, scaled to the glyph.
                readonly property real s: width / 12
                visible: hover.hovered
                x: parent.width + Math.max(0, (Type.padding * 2 - width) / 2)
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(Type.bodyPixels * 0.9)
                height: width
                preferredRendererType: Shape.CurveRenderer
                Accessible.ignored: true
                // A pencil from the lower left to the upper right.
                ShapePath {
                    strokeColor: Theme.textSecondary
                    strokeWidth: Math.max(1, pencil.s * 1.1)
                    fillColor: "transparent"
                    joinStyle: ShapePath.RoundJoin
                    capStyle: ShapePath.RoundCap
                    startX: pencil.s * 1.5
                    startY: pencil.s * 10.5
                    PathLine { x: pencil.s * 2.3; y: pencil.s * 7.6 }
                    PathLine { x: pencil.s * 8.4; y: pencil.s * 1.5 }
                    PathLine { x: pencil.s * 10.5; y: pencil.s * 3.6 }
                    PathLine { x: pencil.s * 4.4; y: pencil.s * 9.7 }
                    PathLine { x: pencil.s * 1.5; y: pencil.s * 10.5 }
                    PathMove { x: pencil.s * 7.0; y: pencil.s * 2.9 }
                    PathLine { x: pencil.s * 9.1; y: pencil.s * 5.0 }
                }
            }
            Basic.TextField {
                id: input
                objectName: "scalarEditor"
                property string scalarName: cell.name
                visible: cell.open && (!cell.choices || cell.manualCode)
                // Its whole text and the cursor wide, growing leftwards over the
                // owner's room; never narrower than the cell.
                readonly property real needed: Math.ceil(inputMetrics.advanceWidth) + leftPadding + rightPadding + 3
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.max(parent.width, Math.min(parent.width + Math.max(0, cell.editorRoom), needed))
                // Grown to fit, the text shows from its start.
                onWidthChanged: if (needed <= width) ensureVisible(0)
                leftPadding: Type.tight + 1
                rightPadding: Type.tight + 1
                topPadding: 0
                bottomPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                horizontalAlignment: cell.numeric || cell.manualCode ? TextInput.AlignRight : TextInput.AlignLeft
                font.family: Type.fixedFamily
                color: Theme.textPrimary
                selectByMouse: true
                placeholderText: cell.manualCode ? qsTr("Numeric code") : ""
                placeholderTextColor: Theme.textMuted
                background: Rectangle {
                    color: Theme.surfaceWindow
                    border.width: 1
                    border.color: input.activeFocus ? Theme.focusRing : Theme.outline
                    radius: Type.radius
                }
                Accessible.name: qsTr("Value for %1").arg(cell.name)
                Accessible.description: cell.invalid ? cell.takes : cell.detail
                // The same text as the row's writes nothing (no step to undo).
                onTextEdited: if (typeof cell.setText === "function")
                    cell.invalid = text === cell.editorText ? false : !cell.setText(text)
                TextMetrics {
                    id: inputMetrics
                    font: input.font
                    text: input.text.length > 0 ? input.text : input.placeholderText
                }
                onAccepted: cell.keep(true)
                Keys.onEscapePressed: function(event) { cell.revert(); event.accepted = true }
                Keys.onTabPressed: function(event) { cell.keep(); cell.tabbed(true); event.accepted = true }
                Keys.onBacktabPressed: function(event) { cell.keep(); cell.tabbed(false); event.accepted = true }
                onActiveFocusChanged: if (!activeFocus && visible) cell.keep()
            }
            ComboBox {
                id: choice
                objectName: "valueChoice"
                visible: cell.open && cell.choices && !cell.manualCode
                height: parent.height
                // As wide as its widest item, and a little room for the arrow.
                width: Math.min(parent.width, implicitContentWidth + leftPadding + rightPadding + Type.bodyPixels * 1.8)
                anchors.right: cell.numeric ? parent.right : undefined
                implicitContentWidthPolicy: ComboBox.WidestText
                topPadding: 0
                bottomPadding: 0
                palette.light: Theme.stateSelected
                palette.midlight: Theme.stateHover
                palette.mid: Theme.buttonFillPressed
                palette.dark: Theme.textSecondary
                palette.text: Theme.textPrimary
                palette.buttonText: Theme.textPrimary
                palette.highlight: Theme.stateSelected
                palette.highlightedText: Theme.selectedForeground
                textRole: "label"
                valueRole: "text"
                model: cell.options
                currentIndex: cell.optionIndex
                Accessible.name: qsTr("Value for %1").arg(cell.name)
                Accessible.description: cell.choicesTruncated
                    ? qsTr("The list is limited. Choose Enter code to use another numeric code.") : cell.detail
                onActivated: function(index) {
                    const item = cell.options[index]
                    if (!item)
                        return
                    if (item.manual) {
                        cell.manualCode = true
                        input.text = ""
                        input.forceActiveFocus()
                        return
                    }
                    if (typeof cell.setText === "function")
                        cell.invalid = !cell.setText(item.text)
                    currentIndex = Qt.binding(function() { return cell.optionIndex })
                    cell.keep(true)
                }
                Keys.onEscapePressed: function(event) { cell.revert(); event.accepted = true }
                Keys.onTabPressed: function(event) { cell.keep(); cell.tabbed(true); event.accepted = true }
                Keys.onBacktabPressed: function(event) { cell.keep(); cell.tabbed(false); event.accepted = true }
                // Closed without a choice: the value stays as it was.
                popup.onClosed: popupClosed.restart()
                Timer {
                    id: popupClosed
                    interval: 0
                    onTriggered: if (cell.open && cell.choices && !cell.manualCode) cell.keep()
                }
                onActiveFocusChanged: if (!activeFocus && visible && !popup.visible) cell.keep()
            }
        }
        // Words only for text that is no value; floats say what they encode.
        Label {
            id: caption
            objectName: "valueCaption"
            readonly property bool encoded: cell.open && !cell.invalid && cell.floating && cell.canonicalText.length > 0
                                            && input.text.length > 0 && input.text !== cell.canonicalText
            readonly property bool refused: !cell.open && cell.refusal.length > 0
            visible: cell.invalid || encoded || refused
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.pointSize: Type.caption
            color: cell.invalid || refused ? Theme.error : Theme.textSecondary
            horizontalAlignment: cell.numeric ? Text.AlignRight : Text.AlignLeft
            text: cell.invalid ? cell.takesText
                : refused ? cell.refusal
                : encoded ? qsTr("Encoded: %1 (%2)").arg(cell.canonicalText).arg(cell.typeText) : ""
        }
        Button {
            visible: cell.isAggregate && cell.childCount > 128
            enabled: cell.loadedCount < cell.childCount
            text: qsTr("Load next 128 · %1/%2").arg(cell.loadedCount).arg(cell.childCount)
            onClicked: if (typeof cell.loadMore === "function") cell.loadMore()
        }
    }
}
