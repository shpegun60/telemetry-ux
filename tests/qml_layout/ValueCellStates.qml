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
    height: 260
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
    }
}
