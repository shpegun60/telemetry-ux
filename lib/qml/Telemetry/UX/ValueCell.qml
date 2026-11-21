// One value in Details' and the Batch's tables, and the Watchlist's editor: a
// value until it is opened, then edited in place (family spec C). The owner
// gives it the row's roles and the way to write text into the row; edits are
// live. Pure QML, so a host needs none of the module's C++ types for it. MIT.
//
// Keys while open (WL-9 C2, the same in Details and the Watchlist): Enter keeps
// the value and closes; on text that is no value Enter (and Ctrl+Enter) is
// refused - the editor stays open, its border and line 2 in the error colour,
// and the refusal is announced. Esc, Tab, Shift+Tab and a click away always
// leave, whatever the text: Esc puts back the value the cell had when it was
// opened; Tab and a click keep a value, and with text that is no value the
// Watchlist holds it (textHeld) while Details puts back the last value and
// says "Not changed: “300” · u8 takes 0 to 255." under the cell. Tab and
// Shift+Tab also ask the owner for the next or previous cell (tabbed).
// Ctrl+Enter keeps the value and goes on to the owner (Apply, Execute or
// Call), never from text that is no value.
//
// Draft mode (the Watchlist): with `commitText` set the cell edits a draft.
// Typing only asks `checkText`; keeping it (Enter, Tab, focus going elsewhere,
// a choice) calls `commitText(text)` once when the text is a value and differs
// from what the cell was opened with; Escape calls nothing. Closed, the cell
// shows a text the owner holds (heldText, struck through with the error glyph,
// its reason under it, the way back in its tooltip: heldHint), a write under
// way (pendingText, with a progress line after 400 ms), or its value - a
// bool's or an enum's word with its code in the secondary colour (valueWord,
// valueCode). Opened again, a held text is edited as it was typed, and any
// value left in it is written, the device's own too: only a write lets go.
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
    // What the cell says while its text is no value: "u8 takes 0 to 255."
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
    // A bool's or an enum's word and its code in the row's format (WL-9 B2):
    // "Idle" and "2" show as "Idle (2)", the code in the secondary colour and
    // never elided. Empty: the value as it is.
    property string valueWord: ""
    property string valueCode: ""
    // A text the owner holds in the row (WL-9 C4): one the device refused, or
    // one that is no value left by Tab or a click; and why, under it.
    property string heldText: ""
    property string heldReason: ""
    // The way back, at the end of the held text's tooltip: "F2 to change it
    // · Esc drops it" (the owner's keys).
    property string heldHint: ""
    // A write under way (WL-9 C5): the text written, shown in the secondary
    // colour until the outcome, with a progress line after 400 ms.
    property string pendingText: ""
    readonly property bool pending: pendingText.length > 0
    readonly property bool held: heldText.length > 0 && !pending
    // A quiet line under the closed cell: "Not applied · device holds 3"
    // after Details' Apply was refused (WL-9 C4).
    property string standingNote: ""
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
    // The width of what the cell shows now: its editor and a held text grow leftwards over the owner's room.
    readonly property real editorWidth: !open ? (held ? Math.max(width, heldRow.width) : width)
                                              : (choices && !manualCode ? choice.width : input.width)
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
    // A choice opens with its list shown (Details); false: closed, Space or a click opens it.
    property bool listOpensOnBegin: true
    // The value changed on the device since it was last shown: Theme.info.
    property bool changed: false
    // No value yet - not read, not called - shows as "–", muted.
    readonly property bool noValue: !isAggregate && value === "–"
    // The value differs from the one shown before the action that just
    // ended (a read, a call, a run): the cell flashes once, a fading accent
    // fill of 600 ms, so the user sees what changed. updatedAt is when the
    // owner's rows were made again; a cell made later than a second after
    // (a branch opened afterwards) does not flash.
    property bool updated: false
    property double updatedAt: 0
    // No fading: the value simply shows.
    property bool reducedMotion: false
    // How many times the cell has flashed, for a host's check.
    property int flashes: 0
    readonly property bool flashing: flashAnimation.running
    function flash() {
        if (reducedMotion)
            return
        ++flashes
        flashAnimation.restart()
    }
    // Once for each time the owner's rows were made again, however the cell
    // came to show the row (made, or reused from the rows before).
    property double flashedFor: 0
    function maybeFlash() {
        if (updated && !isAggregate && updatedAt > 0 && updatedAt !== flashedFor && Date.now() - updatedAt < 1000) {
            flashedFor = updatedAt
            flash()
        }
    }
    onUpdatedChanged: maybeFlash()
    Component.onCompleted: maybeFlash()
    // Words added to the editor's accessible description.
    property string extraDescription: ""
    // Room to the cell's left up to the name's x, to its right up to the
    // row's last column edge, and the room a folded summary may take to the
    // left (up to the name's text end + the column gap). All 0 by default.
    property real captionRoom: 0
    property real captionRightRoom: 0
    property real restRoom: 0

    // Numbers and true/false stand right, as the Device tree's do; so does a
    // word with its code.
    readonly property bool numeric: valueWord.length > 0
        || /^(?:[usi](?:8|16|32|64)|f(?:32|64)|u?int(?:8|16|32|64)|float(?:32|64)|bool)$/.test(typeText)
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
    // Opened on a held text (draft mode): any value left in it is written,
    // the device's own too, since only a write lets go of a held text.
    property bool openedHeld: false
    property bool closing: false
    // Why the last text left was not kept (Details): "Not changed: “300” ·
    // u8 takes 0 to 255.", under the closed cell until it is opened again.
    property string refusal: ""
    // The words a draft's check gave for the text typed, and its preview.
    property string checkError: ""
    property string checkPreview: ""
    // What the cell said to a screen reader last, and how many times it
    // spoke (a refused Enter), for a host's check.
    property string lastAnnounced: ""
    property int announced: 0

    // Tab or Shift+Tab: the owner opens the next or previous cell.
    signal tabbed(bool forward)
    // The editor closed; byKey when by Enter or Escape, so the owner takes
    // the keyboard back (not when focus went elsewhere).
    signal finished(bool byKey)
    // Draft mode: text that is no value left by Tab or a click away, for the
    // owner to hold in its row with its reason ("Not written: …").
    signal textHeld(string text, string reason)

    implicitHeight: column.implicitHeight
    implicitWidth: Type.bodyPixels * 8

    // Opens the editor; false when this value cannot be edited now. A held
    // text opens as it was typed, selected.
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
        const fromHeld = draftMode && heldText.length > 0 && !choices
        openedHeld = fromHeld
        input.text = fromHeld ? heldText : editorText
        if (choices) {
            choice.forceActiveFocus()
            if (listOpensOnBegin)
                choice.popup.open()
        } else {
            input.forceActiveFocus()
            input.selectAll()
            if (fromHeld)
                check(input.text)
        }
        return true
    }
    // The text typed against the row: a draft asks its owner, a live cell
    // writes it (a text that is no value is not taken).
    function check(text) {
        if (draftMode) {
            const verdict = typeof checkText === "function" ? checkText(text) : ({ ok: true })
            invalid = text === startText ? false : !(verdict && verdict.ok !== false)
            checkError = verdict && typeof verdict.error === "string" ? verdict.error : ""
            checkPreview = verdict && typeof verdict.preview === "string" ? verdict.preview : ""
        } else if (typeof setText === "function") {
            invalid = text === editorText ? false : !setText(text)
        }
    }
    // What the editor holds now.
    function currentText() {
        return choices && !manualCode ? startText : input.text
    }
    // Why the text is no value, in the words line 2 shows.
    function whyInvalid() {
        return draftMode && checkError.length > 0 ? checkError : takesText
    }
    // Draft mode: the text goes to the owner once, when it is a value that
    // differs from the value the cell was opened on, or any value when it
    // was opened on a held text.
    function commitDraft(text) {
        if (draftMode && (text !== startText || openedHeld))
            commitText(text)
    }
    // Draft mode: what is left by Tab or a click with text that is no value.
    function holdText(text, reason) {
        textHeld(text, reason)
    }
    function announce(words) {
        lastAnnounced = words
        ++announced
        input.Accessible.announce(words)
    }
    // Closes it, keeping what was typed when it is a value; true when it
    // closed. leaving: Tab or focus going elsewhere, which always close.
    // Otherwise (Enter) text that is no value keeps the editor open and is
    // announced, so a refused Enter changes nothing on screen.
    function keep(byKey, leaving) {
        if (!open || closing)
            return true
        if (invalid && leaving !== true) {
            announce(whyInvalid())
            return false
        }
        closing = true
        if (invalid) {
            // Text that is no value is never dropped in silence: the
            // Watchlist holds it in its row; Details puts the last value back
            // and says so, echoing the text, which is nowhere else now.
            if (draftMode) {
                holdText(input.text, qsTr("Not written: %1").arg(whyInvalid()))
            } else {
                refusal = qsTr("Not changed: “%1” · %2").arg(input.text).arg(whyInvalid())
                if (typeof setText === "function")
                    setText(editorText)
            }
        } else if (draftMode) {
            commitDraft(currentText())
        }
        input.deselect()
        invalid = false
        manualCode = false
        open = false
        releaseFocus()
        closing = false
        finished(byKey === true)
        return true
    }
    // Closed, the editors keep no focus in their row's scope: the row's keys
    // (Esc on a held text) reach the row's owner, not a hidden editor.
    function releaseFocus() {
        input.focus = false
        choice.focus = false
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
        releaseFocus()
        closing = false
        finished(true)
    }
    function reset() {
        refusal = ""
        open = false
        invalid = false
        manualCode = false
        input.focus = false
        choice.focus = false
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
    // written is in sight: "false (0)", "Idle (0)", "Idle (0x00)" in hex. A
    // negative code in hex is its bits at the type's width, "Off (0xFF)".
    function codeLabel(item) {
        let label = item.label
        if (editorKind === "bool" && !/\(\d+\)$/.test(label))
            label = item.text + (item.text === "true" ? " (1)" : " (0)")
        if (!hexCodes)
            return label
        const found = /^(.*) \((-?\d+)\)$/.exec(label)
        if (!found)
            return label
        const bits = /(?:8|16|32|64)/.exec(typeText)
        const digits = (bits ? Number(bits[0]) : 8) / 4
        const code = Number(found[2])
        if (code >= 0)
            return found[1] + " (0x" + code.toString(16).toUpperCase().padStart(digits, "0") + ")"
        // Two's complement at the width: every digit of |code| - 1 turned.
        const turned = (-code - 1).toString(16).padStart(digits, "0").split("")
                       .map(function(digit) { return (15 - parseInt(digit, 16)).toString(16) }).join("")
        return found[1] + " (0x" + turned.toUpperCase() + ")"
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
            // A value that changed with the action just ended: the accent,
            // fading out once.
            Rectangle {
                id: flashFill
                objectName: "valueFlash"
                anchors.fill: parent
                radius: Type.radius
                color: Theme.accent
                opacity: 0
                Accessible.ignored: true
                OpacityAnimator {
                    id: flashAnimation
                    target: flashFill
                    from: 0.4
                    to: 0
                    duration: 600
                    easing.type: Easing.OutQuad
                }
            }
            Label {
                id: shown
                objectName: "valueText"
                property string scalarName: cell.name
                visible: !cell.open && !cell.isAggregate && !cell.held && !cell.pending && cell.valueWord.length === 0
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                elide: Text.ElideRight
                // An enum reads its name with the code, as its list does.
                text: cell.editorKind === "enum" && cell.optionIndex >= 0 && !cell.isAggregate
                      ? cell.options[cell.optionIndex].label : cell.value
                color: cell.highlighted ? Theme.selectedForeground
                    : cell.stale || cell.noValue ? Theme.textMuted : cell.changed ? Theme.info : Theme.textPrimary
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
            // A bool's or an enum's word with its code (WL-9 B2): one row
            // right-aligned on the value's edge; the word elides, the code
            // never.
            Row {
                id: twoTone
                objectName: "valueTwoTone"
                visible: !cell.open && !cell.isAggregate && !cell.held && !cell.pending && cell.valueWord.length > 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    id: wordText
                    objectName: "valueWordText"
                    width: Math.max(0, Math.min(implicitWidth, line.width - codeText.implicitWidth))
                    textFormat: Text.PlainText
                    text: cell.valueWord
                    elide: Text.ElideRight
                    font.family: Type.fixedFamily
                    font.italic: cell.stale
                    color: cell.highlighted ? Theme.selectedForeground
                         : cell.stale ? Theme.textMuted : cell.changed ? Theme.info : Theme.textPrimary
                }
                Text {
                    id: codeText
                    objectName: "valueCodeText"
                    visible: cell.valueCode.length > 0
                    textFormat: Text.PlainText
                    text: " (" + cell.valueCode + ")"
                    font.family: Type.fixedFamily
                    font.italic: cell.stale
                    color: cell.highlighted ? Theme.selectedForeground
                         : cell.stale ? Theme.textMuted : cell.changed ? Theme.info : Theme.textSecondary
                }
            }
            // A held text (WL-9 C4): the error glyph, then the text struck
            // through, right-aligned on the value's edge; it may grow left over
            // the owner's room as the editor does, and elides in its middle
            // past that. Never measured into the column.
            Row {
                id: heldRow
                objectName: "valueHeld"
                visible: !cell.open && cell.held
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Type.spacing
                // The whole text when it is cut, then the way back.
                HoverHandler { id: heldHover }
                PlainToolTip {
                    objectName: "valueHeldToolTip"
                    delay: 700
                    readonly property string tip: [heldLabel.truncated ? cell.heldText : "", cell.heldHint]
                        .filter(function(part) { return part.length > 0 }).join(" · ")
                    visible: heldHover.hovered && tip.length > 0
                    text: tip
                }
                ErrorGlyph {
                    id: heldGlyph
                    objectName: "valueHeldGlyph"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    id: heldLabel
                    objectName: "valueHeldText"
                    readonly property real room: cell.width + Math.max(0, cell.editorRoom) - heldGlyph.width - heldRow.spacing
                    width: Math.max(0, Math.min(implicitWidth, room))
                    textFormat: Text.PlainText
                    text: cell.heldText
                    elide: Text.ElideMiddle
                    font.family: Type.fixedFamily
                    font.strikeout: true
                    // The error colour stays on a selection (4.53:1).
                    color: Theme.error
                }
            }
            // A write under way: what was typed, quiet, until the outcome.
            Label {
                id: pendingLabel
                objectName: "valuePending"
                visible: !cell.open && !cell.isAggregate && cell.pending
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                elide: Text.ElideRight
                text: cell.pendingText
                font.family: Type.fixedFamily
                color: cell.highlighted ? Theme.selectedForeground : Theme.textSecondary
                horizontalAlignment: cell.numeric ? Text.AlignRight : Text.AlignLeft
            }
            // Past 400 ms a 2 px line along the bottom of the value's line;
            // with reduced motion a still one.
            ProgressLine {
                objectName: "valueProgress"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                thickness: 2
                visible: !cell.open && cell.pending
                running: cell.pending
                reducedMotion: cell.reducedMotion
                description: qsTr("Writing %1").arg(cell.name)
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
                color: cell.highlighted ? Theme.selectedForeground
                    : cell.stale || text === "–" ? Theme.textMuted : Theme.textSecondary
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
                // Text that is no value: the border in the error colour while
                // it is typed, so a refused Enter changes nothing on screen.
                background: Rectangle {
                    objectName: "scalarEditorBorder"
                    color: Theme.surfaceWindow
                    border.width: 1
                    border.color: cell.invalid ? Theme.error : input.activeFocus ? Theme.focusRing : Theme.outline
                    radius: Type.radius
                }
                Accessible.name: qsTr("Value for %1").arg(cell.name)
                Accessible.description: [cell.invalid ? cell.whyInvalid() : cell.detail,
                                         captionBox.closedWords.length > 0 ? "" : caption.truncated ? captionBox.words : "",
                                         cell.extraDescription].filter(function(part) { return part.length > 0 }).join(" ")
                // The same text as the row's writes nothing (no step to undo).
                onTextEdited: cell.check(text)
                TextMetrics {
                    id: inputMetrics
                    font: input.font
                    text: input.text.length > 0 ? input.text : input.placeholderText
                }
                // A TextField hands Enter on to its parents after `accepted`,
                // and the table behind it opened the cell again with all its
                // text selected. Enter is taken here: it keeps and closes, and
                // only Ctrl+Enter goes on (to Apply, Execute or Call) - never
                // when the text is no value, which keeps the editor open.
                Keys.onReturnPressed: function(event) {
                    event.accepted = !cell.keep(true) || !(event.modifiers & Qt.ControlModifier)
                }
                Keys.onEnterPressed: function(event) {
                    event.accepted = !cell.keep(true) || !(event.modifiers & Qt.ControlModifier)
                }
                Keys.onEscapePressed: function(event) { event.accepted = cell.open; cell.revert() }
                Keys.onTabPressed: function(event) { cell.keep(false, true); cell.tabbed(true); event.accepted = true }
                Keys.onBacktabPressed: function(event) { cell.keep(false, true); cell.tabbed(false); event.accepted = true }
                onActiveFocusChanged: if (!activeFocus && visible) cell.keep(false, true)
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
                    // The box shows the cell's value again, not the entry just picked.
                    currentIndex = Qt.binding(function() { return cell.optionIndex })
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
                    cell.keep(true, true)
                }
                Keys.onEscapePressed: function(event) { event.accepted = cell.open; cell.revert() }
                Keys.onTabPressed: function(event) { cell.keep(false, true); cell.tabbed(true); event.accepted = true }
                Keys.onBacktabPressed: function(event) { cell.keep(false, true); cell.tabbed(false); event.accepted = true }
                // Closed without a choice: the value stays as it was.
                popup.onClosed: popupClosed.restart()
                Timer {
                    id: popupClosed
                    interval: 0
                    onTriggered: if (cell.open && cell.choices && !cell.manualCode) cell.keep(false, true)
                }
                onActiveFocusChanged: if (!activeFocus && visible && !popup.visible) cell.keep(false, true)
            }
        }
        // Line 2, one quiet line whole (WL-9 C3): what a float encodes, a
        // preview, the owner's note, the words for text that is no value, a
        // held text's reason, or a note under the closed cell. Its height is
        // its laid-out text, with `spacing` below, so the row's focus frame
        // never covers it. One line right-aligned on the value's edge when it
        // fits left of it to the name; else on the column group's edge; else
        // wrapped from the name's x, up to three lines, then cut, the whole
        // in its tooltip and the editor's description.
        Item {
            id: captionBox
            objectName: "valueCaptionBox"
            readonly property bool encoded: cell.open && !cell.invalid && !cell.draftMode && cell.floating
                && cell.canonicalText.length > 0 && input.text.length > 0 && input.text !== cell.canonicalText
            readonly property bool refused: !cell.open && cell.refusal.length > 0
            // Under the closed cell: a held text's reason, Details' "Not
            // changed", or its note.
            readonly property string closedWords: cell.open || cell.pending ? ""
                : cell.held ? cell.heldReason : cell.refusal.length > 0 ? cell.refusal : cell.standingNote
            readonly property bool isError: cell.open ? cell.invalid : (cell.held || refused)
            readonly property string words: {
                if (cell.open) {
                    if (cell.invalid)
                        return cell.whyInvalid()
                    const parts = []
                    if (encoded)
                        parts.push(qsTr("Encoded: %1 (%2)").arg(cell.canonicalText).arg(cell.typeText))
                    if (cell.draftMode && cell.checkPreview.length > 0)
                        parts.push(cell.checkPreview.charAt(0) === "=" ? cell.checkPreview : "= " + cell.checkPreview)
                    if (cell.note.length > 0)
                        parts.push(cell.note)
                    return parts.join(" · ")
                }
                return closedWords
            }
            visible: words.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? Math.ceil(caption.contentHeight) : 0
            Layout.bottomMargin: visible ? Type.spacing : 0
            Label {
                id: caption
                objectName: "valueCaption"
                readonly property real natural: Math.ceil(captionMetrics.advanceWidth) + 1
                readonly property real nearRoom: cell.width + cell.captionRoom
                readonly property real allRoom: nearRoom + cell.captionRightRoom
                readonly property bool nearFits: natural <= nearRoom
                readonly property bool wraps: natural > allRoom
                x: -cell.captionRoom
                width: nearFits ? nearRoom : allRoom
                textFormat: Text.PlainText
                wrapMode: wraps ? Text.Wrap : Text.NoWrap
                maximumLineCount: 3
                elide: Text.ElideRight
                font.pointSize: Type.caption
                horizontalAlignment: wraps ? Text.AlignLeft : Text.AlignRight
                // Errors and held reasons keep the error colour on a selection.
                color: captionBox.isError ? Theme.error : cell.highlighted ? Theme.selectedForeground : Theme.textSecondary
                text: captionBox.words
                Accessible.ignored: true
                HoverHandler { id: captionHover }
                PlainToolTip {
                    objectName: "valueCaptionToolTip"
                    delay: 700
                    readonly property string tip: cell.open && cell.note.length > 0 && cell.noteDetail.length > 0 && !captionBox.isError
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
