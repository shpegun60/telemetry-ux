// The Find bar the Viewer's views share, as Hex has it: what to find, which
// match of how many, Previous and Next, a word when it wrapped or found
// nothing, and Close. Finding starts as the text is typed. The view answers
// findRequested(forward) and closed(), and fills countText and message.
// Keys in the field: Enter next, Shift+Enter previous, Esc closes; the view
// adds F3 and Shift+F3. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: bar

    property alias text: field.text
    // "3 of 41", "100000+" - empty when there is nothing to count.
    property string countText: ""
    // "Wrapped to the start" is news; "Not found" is not found.
    property string message: ""
    property bool caseOption: false
    property alias matchCase: caseBox.checked
    property alias field: field

    signal findRequested(bool forward)
    signal closed()

    function open() {
        visible = true
        Qt.callLater(function() {
            field.forceActiveFocus()
            field.selectAll()
        })
    }
    function close() {
        visible = false
        message = ""
        bar.closed()
    }

    visible: false
    spacing: 8

    Label {
        text: qsTr("Find")
        color: Theme.textSecondary
    }
    TextField {
        id: field
        objectName: bar.objectName + "Field"
        // It gives way first when the Viewer is narrow.
        Layout.fillWidth: true
        Layout.minimumWidth: 96
        Layout.maximumWidth: 260
        placeholderText: qsTr("Text to find")
        Accessible.name: qsTr("Find")
        onTextChanged: if (bar.visible) bar.findRequested(true)
        onAccepted: bar.findRequested(true)
        Keys.onPressed: function(event) {
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ShiftModifier)) {
                bar.findRequested(false)
                event.accepted = true
            }
        }
        Keys.onEscapePressed: bar.close()
    }
    CheckBox {
        id: caseBox
        objectName: bar.objectName + "Case"
        visible: bar.caseOption
        text: qsTr("Match case")
        onToggled: if (field.text.length > 0) bar.findRequested(true)
    }
    Label {
        objectName: bar.objectName + "Count"
        visible: field.text.length > 0 && bar.countText.length > 0
        color: Theme.textSecondary
        font.pointSize: Type.caption
        text: bar.countText
    }
    LinkButton {
        objectName: bar.objectName + "Previous"
        tint: Theme.textSecondary
        text: qsTr("Previous")
        onClicked: bar.findRequested(false)
    }
    LinkButton {
        objectName: bar.objectName + "Next"
        tint: Theme.textSecondary
        text: qsTr("Next")
        onClicked: bar.findRequested(true)
    }
    Label {
        objectName: bar.objectName + "Message"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        color: bar.message.indexOf(qsTr("Wrapped")) === 0 ? Theme.textSecondary : Theme.error
        elide: Text.ElideRight
        text: bar.message
        Accessible.role: Accessible.AlertMessage
    }
    ToolButton {
        objectName: bar.objectName + "Close"
        flat: true
        implicitWidth: 24
        implicitHeight: 24
        text: qsTr("×")
        Accessible.name: qsTr("Close Find")
        ToolTip.visible: hovered || visualFocus
        ToolTip.text: qsTr("Close (Esc)")
        onClicked: bar.close()
    }
}
