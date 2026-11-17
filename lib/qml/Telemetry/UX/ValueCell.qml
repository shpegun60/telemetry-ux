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
//
// Draft mode (the Watchlist): with `commitText` set the cell edits a draft.
// Typing only asks `checkText`; keeping it (Enter, Tab, focus going elsewhere,
// a choice) calls `commitText(text)` once when the text is a value and differs
// from what the cell was opened with; Escape calls nothing.
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
    // Measured from the text, never from the elided label, so a narrow column
    // never reports a narrow value.
    readonly property real textWidth: isAggregate ? 0 : Math.ceil(shownMetrics.advanceWidth)
    // Nothing opens: changes are off, or the owner is busy. The value stays.
    property bool locked: false
    // Writes text into the row; returns whether the text is a value.
    property var setText: null
    // Loads the next 128 members of a large struct or array.
    property var loadMore: null
    // A branch row: open (its members show below) or folded. A folded branch
    // shows its summary in the Value column and over the Name's spare room.
    property bool expanded: false
    property var summaryParts: []
    property string summaryKind: ""
    // Elements or members under a branch (an array shown as ranges holds more than childCount); -1: childCount.
    property int itemCount: -1
    // The words a folded branch shows now, for the row's accessible description.
    readonly property string summaryShown: summary.visible ? summary.text : ''
    // Draft mode (see above): null keeps the live setText.
    property var commitText: null
    // {ok, preview, error} for a typed text, in draft mode.
    property var checkText: null
    // The owner's quiet line under the open cell, and its hover tooltip.
    property string note: ""
    property string noteDetail: ""
    // False: the owner calls begin() itself (the Watchlist's double-click).
    property bool openOnClick: true
    // The value changed on the device since it was last shown: Theme.info.
    property bool changed: false
    // Words added to the editor's accessible description.
    property string extraDescription: ""
    // Room to the cell's left up to the name's x, to its right up to the
    // row's last column edge, and the room a folded summary may take to the
    // left (up to the name's text end + the column gap). All 0 by default.
    property real captionRoom: 0
    property real captionRightRoom: 0
    property real restRoom: 0

    // Numbers and true/false stand right, as the Device tree's do.
    readonly property bool numeric: /^(?:[usi](?:8|16|32|64)|f(?:32|64)|u?int(?:8|16|32|64)|float(?:32|64)|bool)$/.test(typeText)
    readonly property bool floating: /^(?:f(?:32|64)|float(?:32|64))$/.test(typeText)
    readonly property bool choices: editorKind === "bool" || editorKind === "enum"
    readonly property bool draftMode: typeof commitText === "function"
    readonly property bool canOpen: editable && !locked && !isAggregate && (typeof setText === "function" || draftMode)
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
    // The words a draft's check gave for the text typed, and its preview.
    property string checkError: ""
    property string checkPreview: ""

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
        checkError = ""
        checkPreview = ""
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
    // What the editor holds now.
    function currentText() {
        return choices && !manualCode ? startText : input.text
    }
    // Draft mode: the text goes to the owner once, when it is a value that
    // differs from what the cell was opened with.
    function commitDraft(text) {
        if (draftMode && text !== startText)
            commitText(text)
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
            refusal = qsTr("Not changed: %1").arg(draftMode && checkError.length > 0 ? checkError : takesText)
            if (!draftMode && typeof setText === "function")
                setText(editorText)
        } else if (draftMode) {
            commitDraft(currentText())
        }
        input.deselect()
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
        if (!draftMode && typeof setText === "function" && editorText !== startText)
            setText(startText)
        input.deselect()
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
    // The exact value a float cell encodes and its bits, for the caption's tooltip.
    function exactText(text) {
        const number = Number(text)
        if (!isFinite(number) || text.length === 0)
            return ""
        const bytes = new DataView(new ArrayBuffer(8))
        const hex = function(value) { return value.toString(16).toUpperCase().padStart(8, "0") }
        if (/^(?:f32|float32)$/.test(typeText)) {
            const single = Math.fround(number)
            bytes.setFloat32(0, single)
            return String(single) + " · 0x" + hex(bytes.getUint32(0))
        }
        bytes.setFloat64(0, number)
        return String(number) + " · 0x" + hex(bytes.getUint32(0)) + hex(bytes.getUint32(4))
    }
    // The folded summary that fits `room` characters: whole parts only, then
    // the tail ("…}" or "… 64 items]"); "{…}" / "[… N items]" when none fits.
    function summaryFor(room) {
        const count = function(n) { return n === 1 ? qsTr("1 item") : qsTr("%L1 items").arg(n) }
        const parts = summaryParts || []
        const total = itemCount >= 0 ? itemCount : childCount
        if (summaryKind === "arrayOfAggregates")
            return "[" + count(total) + "]"
        const isStruct = summaryKind === "struct"
        const open = isStruct ? "{" : "[", close = isStruct ? "}" : "]"
        const tail = isStruct ? "…" : "… " + count(total)
        const complete = parts.length >= total
        if (complete) {
            const whole = open + parts.join(", ") + close
            if (whole.length <= room)
                return whole
        }
        for (let k = complete ? parts.length - 1 : parts.length; k >= 1; --k) {
            const text = open + parts.slice(0, k).join(", ") + ", " + tail + close
            if (text.length <= room)
                return text
        }
        return open + tail + close
    }
    // Everything the model gave, up to its eight parts, for the tooltip.
    function summaryAll() {
        return summaryFor(1e9)
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

            HoverHandler { id: hover; enabled: cell.canOpen && !cell.open && cell.openOnClick }
            TapHandler {
                enabled: cell.canOpen && !cell.open && cell.openOnClick
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
                visible: !cell.open && !cell.isAggregate
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                elide: Text.ElideRight
                // An enum reads its name with the code, as its list does.
                text: cell.editorKind === "enum" && cell.optionIndex >= 0 && !cell.isAggregate
                      ? cell.options[cell.optionIndex].label : cell.value
                color: cell.highlighted ? Theme.selectedForeground
                    : cell.stale ? Theme.textMuted : cell.changed ? Theme.info : Theme.textPrimary
                font.family: Type.fixedFamily
                font.italic: cell.stale
                horizontalAlignment: cell.numeric ? Text.AlignRight : Text.AlignLeft
                Accessible.description: cell.stale ? qsTr("From an earlier run; the values have changed since.") : ""
                HoverHandler { id: shownHover }
                PlainToolTip {
                    visible: shownHover.hovered && (shown.truncated || (cell.stale && cell.staleNote.length > 0))
                    text: cell.stale && cell.staleNote.length > 0 ? cell.value + " · " + cell.staleNote : cell.value
                }
                TextMetrics { id: shownMetrics; font: shown.font; text: shown.text }
            }
            // A folded branch: its members' values, right-aligned to the
            // Value column's edge and reaching left over the Name's spare
            // room. Never part of the column's measured width.
            Label {
                id: summary
                objectName: "foldedSummary"
                visible: cell.isAggregate && !cell.expanded && !cell.open
                x: -cell.restRoom
                width: parent.width + cell.restRoom
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignRight
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.family: Type.fixedFamily
                font.italic: cell.stale
                color: cell.highlighted ? Theme.selectedForeground : cell.stale ? Theme.textMuted : Theme.textSecondary
                readonly property real advance: summaryMetrics.advanceWidth
                text: cell.summaryKind.length === 0 ? (cell.value.length > 0 ? cell.value : "–")
                    : cell.summaryFor(advance > 0 ? Math.floor(width / advance) : 0)
                Accessible.ignored: true
                HoverHandler { id: summaryHover }
                PlainToolTip {
                    objectName: "foldedSummaryToolTip"
                    delay: 700
                    visible: summaryHover.hovered && cell.summaryKind.length > 0 && summary.text !== cell.summaryAll()
                    text: cell.summaryAll()
                }
                TextMetrics { id: summaryMetrics; font: summary.font; text: "0" }
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
                Accessible.description: [cell.invalid ? (cell.draftMode && cell.checkError.length > 0 ? cell.checkError : cell.takes)
                                                      : cell.detail, cell.extraDescription].filter(function(part) { return part.length > 0 }).join(" ")
                // The same text as the row's writes nothing (no step to undo).
                onTextEdited: {
                    if (cell.draftMode) {
                        const verdict = typeof cell.checkText === "function" ? cell.checkText(text) : ({ ok: true })
                        cell.invalid = text === cell.startText ? false : !(verdict && verdict.ok !== false)
                        cell.checkError = verdict && typeof verdict.error === "string" ? verdict.error : ""
                        cell.checkPreview = verdict && typeof verdict.preview === "string" ? verdict.preview : ""
                    } else if (typeof cell.setText === "function") {
                        cell.invalid = text === cell.editorText ? false : !cell.setText(text)
                    }
                }
                TextMetrics {
                    id: inputMetrics
                    font: input.font
                    text: input.text.length > 0 ? input.text : input.placeholderText
                }
                // A TextField hands Enter on to its parents after `accepted`,
                // and the table behind it opened the cell again with all its
                // text selected. Enter is taken here: it keeps and closes, and
                // only Ctrl+Enter goes on (to Apply, Execute or Call).
                Keys.onReturnPressed: function(event) { cell.keep(true); event.accepted = !(event.modifiers & Qt.ControlModifier) }
                Keys.onEnterPressed: function(event) { cell.keep(true); event.accepted = !(event.modifiers & Qt.ControlModifier) }
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
                // As wide as its widest choice with the arrow, "false (0)" or
                // "Active (0x02)" never cut: growing leftwards over the owner's
                // room as the text editor does, the right edge on the cell's.
                readonly property real widestChoice: {
                    let widest = 0
                    for (let i = 0; i < cell.options.length; ++i)
                        widest = Math.max(widest, choiceMetrics.advanceWidth(cell.options[i].label))
                    return Math.ceil(widest)
                }
                readonly property real needed: Math.max(implicitWidth, widestChoice + leftPadding + rightPadding + 2
                    + (contentItem && typeof contentItem.leftPadding === "number"
                       ? contentItem.leftPadding + contentItem.rightPadding : 0))
                width: Math.min(parent.width + Math.max(0, cell.editorRoom), needed)
                x: cell.numeric || width > parent.width ? parent.width - width : 0
                implicitContentWidthPolicy: ComboBox.WidestText
                // The list never cuts a choice either: as wide as the widest in
                // the current one's weight, its right edge on the box's.
                popup.width: Math.max(width, widestChoice + choiceProbe.leftPadding + choiceProbe.rightPadding
                                             + popup.leftPadding + popup.rightPadding + 2)
                popup.x: width - popup.width
                // The list's row as a style draws it, the current one in bold:
                // its padding and its font measure every choice.
                ItemDelegate {
                    id: choiceProbe
                    visible: false
                    font.weight: Font.DemiBold
                    Accessible.ignored: true
                }
                FontMetrics {
                    id: choiceMetrics
                    font: choiceProbe.font
                }
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
                Accessible.description: [cell.choicesTruncated
                    ? qsTr("The list is limited. Choose Enter code to use another numeric code.") : cell.detail,
                    cell.extraDescription].filter(function(part) { return part.length > 0 }).join(" ")
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
                    if (cell.draftMode)
                        cell.commitDraft(item.text)
                    else if (typeof cell.setText === "function")
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
        // Line 2, one line across the row: what a float encodes, a preview,
        // the owner's note, or the words for text that is no value. It takes
        // the room the owner gives to the left and then to the right of the
        // cell, and elides when even that is short.
        Item {
            id: captionBox
            objectName: "valueCaptionBox"
            readonly property bool encoded: cell.open && !cell.invalid && !cell.draftMode && cell.floating
                && cell.canonicalText.length > 0 && input.text.length > 0 && input.text !== cell.canonicalText
            readonly property bool refused: !cell.open && cell.refusal.length > 0
            readonly property bool isError: cell.invalid || refused
            readonly property string words: {
                if (cell.open) {
                    if (cell.invalid)
                        return cell.draftMode && cell.checkError.length > 0 ? cell.checkError : cell.takesText
                    const parts = []
                    if (encoded)
                        parts.push(qsTr("Encoded: %1 (%2)").arg(cell.canonicalText).arg(cell.typeText))
                    if (cell.draftMode && cell.checkPreview.length > 0)
                        parts.push("= " + cell.checkPreview)
                    if (cell.note.length > 0)
                        parts.push(cell.note)
                    return parts.join(" · ")
                }
                return refused ? cell.refusal : ""
            }
            visible: words.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? caption.height : 0
            Label {
                id: caption
                objectName: "valueCaption"
                readonly property real natural: Math.ceil(captionMetrics.advanceWidth) + 1
                readonly property bool nearFits: natural <= cell.width + cell.captionRoom
                x: -cell.captionRoom
                width: cell.width + cell.captionRoom + (nearFits ? 0 : cell.captionRightRoom)
                textFormat: Text.PlainText
                wrapMode: captionBox.isError ? Text.Wrap : Text.NoWrap
                maximumLineCount: captionBox.isError ? 2 : 1
                elide: Text.ElideRight
                font.pointSize: Type.caption
                horizontalAlignment: Text.AlignRight
                color: captionBox.isError ? Theme.error : cell.highlighted ? Theme.selectedForeground : Theme.textSecondary
                text: captionBox.words
                Accessible.ignored: true
                HoverHandler { id: captionHover }
                PlainToolTip {
                    objectName: "valueCaptionToolTip"
                    delay: 700
                    readonly property string tip: cell.note.length > 0 && cell.noteDetail.length > 0 && !captionBox.isError
                        ? cell.noteDetail
                        : captionBox.encoded ? cell.exactText(cell.canonicalText)
                        : caption.truncated ? captionBox.words : ""
                    visible: captionHover.hovered && tip.length > 0
                    text: tip
                }
                TextMetrics { id: captionMetrics; font: caption.font; text: captionBox.words }
            }
        }
        Button {
            visible: cell.isAggregate && cell.expanded && cell.childCount > 128
            enabled: cell.loadedCount < cell.childCount
            text: qsTr("Load next 128 · %1/%2").arg(cell.loadedCount).arg(cell.childCount)
            onClicked: if (typeof cell.loadMore === "function") cell.loadMore()
        }
    }
}
