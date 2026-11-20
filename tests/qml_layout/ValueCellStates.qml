// The shared value cell alone (BA-4): a number, a bool, an enum, a float and
// a locked number, each over a fake row whose setText checks the text as the
// providers do. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

ApplicationWindow {
    id: window
    width: 420
    height: 480
    visible: true
    color: Theme.surfaceWindow

    // A fake row: the value it holds and what it was asked to take.
    component FakeRow: QtObject {
        property string text: "0"
        property var written: []
        property int tabbedForward: 0
        property int tabbedBack: 0
        property int finishedByKey: 0
        property var accepts: function(text) { return /^-?\d+$/.test(text) && Number(text) >= 0 && Number(text) <= 255 }
        function set(value) {
            written = written.concat([value])
            if (!accepts(value))
                return false
            text = value
            return true
        }
    }
    FakeRow { id: gainRow; objectName: "gainRow"; text: "3" }
    FakeRow { id: enabledRow; objectName: "enabledRow"; text: "false"; accepts: function(t) { return t === "true" || t === "false" } }
    FakeRow { id: modeRow; objectName: "modeRow"; text: "1"; accepts: function(t) { return /^\d+$/.test(t) } }
    FakeRow { id: factorRow; objectName: "factorRow"; text: "0.1"; accepts: function(t) { return !isNaN(Number(t)) && t.length > 0 } }
    FakeRow { id: stateRow; objectName: "stateRow"; text: "2"; accepts: function(t) { return t === "0" || t === "2" } }
    FakeRow { id: piRow; objectName: "piRow"; text: "3.14159274"; accepts: function(t) { return !isNaN(Number(t)) && t.length > 0 } }

    Column {
        x: Type.padding
        y: Type.padding
        width: 240
        spacing: 0
        ValueCell {
            objectName: "gainCell"
            width: parent.width
            name: "Gain"; typeText: "u8"; editable: true
            value: gainRow.text; editorText: gainRow.text
            takes: "Enter a number from 0 to 255."
            setText: function(text) { return gainRow.set(text) }
            onTabbed: function(forward) { if (forward) ++gainRow.tabbedForward; else ++gainRow.tabbedBack }
            onFinished: function(byKey) { if (byKey) ++gainRow.finishedByKey }
        }
        ValueCell {
            objectName: "enabledCell"
            width: parent.width
            name: "Enabled"; typeText: "bool"; editable: true; editorKind: "bool"
            editorChoices: [{ label: "false (0)", text: "false" }, { label: "true (1)", text: "true" }]
            value: enabledRow.text; editorText: enabledRow.text
            takes: "Enter true or false."
            setText: function(text) { return enabledRow.set(text) }
        }
        ValueCell {
            objectName: "modeCell"
            width: parent.width
            name: "Mode"; typeText: "enum (3)"; editable: true; editorKind: "enum"
            editorChoices: [{ label: "Idle (0)", text: "0" }, { label: "Run (1)", text: "1" }, { label: "Stop (2)", text: "2" }]
            value: modeRow.text === "0" ? "Idle (0)" : modeRow.text === "1" ? "Run (1)" : modeRow.text === "2" ? "Stop (2)" : modeRow.text
            editorText: modeRow.text
            takes: "Enter Idle, Run or Stop, or a code."
            setText: function(text) { return modeRow.set(text) }
        }
        ValueCell {
            objectName: "factorCell"
            width: parent.width
            name: "Factor"; typeText: "f32"; editable: true
            value: factorRow.text; editorText: factorRow.text
            canonicalText: factorRow.text === "0.1" ? "0.100000001" : factorRow.text
            takes: "Enter a number."
            setText: function(text) { return factorRow.set(text) }
        }
        ValueCell {
            objectName: "lockedCell"
            width: parent.width
            name: "Locked"; typeText: "u8"; editable: true; locked: true
            value: "7"; editorText: "7"
            setText: function(text) { return false }
        }
        Button {
            objectName: "elsewhere"
            text: "Elsewhere"
        }
        ValueCell {
            objectName: "readOnlyCell"
            width: parent.width
            name: "Measured"; typeText: "f32"
            value: "230.5"
        }
        // A Value column narrower than its value, as Details' is: the owner
        // gives the Name column's room to its left.
        Item {
            width: parent.width
            height: piCell.height
            Label { objectName: "piName"; text: "Pi"; height: Type.rowHeight; verticalAlignment: Text.AlignVCenter }
            ValueCell {
                id: piCell
                objectName: "piCell"
                x: parent.width - width
                width: 60
                editorRoom: parent.width - width - Type.bodyPixels * 2
                // Line 2's room to the left, as Details gives it (WL-9 C3).
                captionRoom: parent.width - width - Type.bodyPixels * 2
                name: "Pi"; typeText: "f32"; editable: true
                value: piRow.text; editorText: piRow.text
                canonicalText: piRow.text === "0.1" ? "0.100000001" : piRow.text
                takes: "Enter a number."
                setText: function(text) { return piRow.set(text) }
            }
        }
        QtObject { id: draftLog; objectName: "draftLog"; property var committed: []; property int checked: 0; property string shown: "3"; property string held: ""; property string heldReason: "" }
        // The Watchlist's cell: a draft, written only when kept.
        ValueCell {
            id: draftCell
            objectName: "draftCell"
            width: parent.width
            name: "Gain"; typeText: "u8"; editable: true
            value: draftLog.shown; editorText: draftLog.shown
            takes: "Enter a number from 0 to 255."
            note: "Writes all 3 members of Config."
            noteDetail: "Enter reads Config, sets Gain in it and writes all 3 members back."
            commitText: function(text) { draftLog.committed = draftLog.committed.concat([text]); draftLog.shown = text }
            heldText: draftLog.held
            heldReason: draftLog.heldReason
            onTextHeld: function(text, reason) { draftLog.held = text; draftLog.heldReason = reason }
            checkText: function(text) {
                draftLog.checked++
                return /^\d+$/.test(text) && Number(text) <= 255 ? ({ ok: true, preview: "" })
                    : ({ ok: false, error: "Enter a number from 0 to 255." })
            }
        }
        // A float whose exact value and bits the caption's tooltip gives.
        ValueCell {
            objectName: "exactCell"
            width: parent.width
            name: "Pi"; typeText: "f32"; editable: true
            value: "3.14159274"; editorText: "3.14159274"
            canonicalText: "3.14159274"
            setText: function(text) { return true }
        }
        // A choice in that narrow column, its codes in hex.
        Item {
            width: parent.width
            height: stateCell.height
            ValueCell {
                id: stateCell
                objectName: "stateCell"
                x: parent.width - width
                width: 60
                editorRoom: parent.width - width - Type.bodyPixels * 2
                hexCodes: true
                name: "State"; typeText: "u8"; editable: true; editorKind: "enum"
                editorChoices: [{ label: "Idle (0)", text: "0" }, { label: "Active (2)", text: "2" }]
                value: stateRow.text === "0" ? "Idle (0)" : stateRow.text === "2" ? "Active (2)" : stateRow.text
                editorText: stateRow.text
                setText: function(text) { return stateRow.set(text) }
            }
        }
    }
}
