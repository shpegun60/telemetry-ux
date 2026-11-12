// The capability codec's view: what a device's read-many.bin or batch.bin
// allows - items in a request, the request and reply sizes, the session it
// holds for, the descriptor it binds to - each with what it means, and Copy.
// A record bound to another descriptor than the connected device's says so in
// the warning colour. MIT.
//
// FileViewer loads it with `decoded`, a CapabilityContent.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: view
    objectName: "capabilityView"

    required property var decoded
    property var content: null

    function focusContent() { copyButton.forceActiveFocus() }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                objectName: "capabilityKind"
                text: view.decoded.kind
                color: Theme.textPrimary
                font.weight: Type.headingWeight
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.pointSize: Type.caption
                color: Theme.textSecondary
                text: view.decoded.purpose
            }
            LinkButton {
                id: copyButton
                objectName: "capabilityCopy"
                tint: Theme.textSecondary
                text: qsTr("Copy")
                Accessible.name: qsTr("Copy every field")
                onClicked: Clipboard.setText(view.decoded.text())
            }
        }

        // Bound to another firmware's descriptor: every limit below is that
        // firmware's, so this comes first.
        Label {
            objectName: "capabilityOtherDescriptor"
            visible: view.decoded.binding === "other"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.warning
            text: qsTr("This record belongs to another firmware: it names descriptor %1, not the connected device's. "
                       + "Its limits may not hold for the device connected now.").arg(view.decoded.fingerprintGroups)
            Accessible.role: Accessible.AlertMessage
        }

        // name · value · what it means, one row each. A screen reader hears a
        // row whole, from its name: the three columns are built one after
        // another, so the value and meaning labels are left out of its order.
        GridLayout {
            objectName: "capabilityFields"
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 16
            rowSpacing: 6
            Repeater {
                model: view.decoded.fields
                delegate: Label {
                    required property var modelData
                    required property int index
                    Layout.row: index
                    Layout.column: 0
                    text: modelData.name
                    color: Theme.textSecondary
                    Accessible.name: qsTr("%1, %2, %3").arg(modelData.name).arg(modelData.value).arg(modelData.meaning)
                }
            }
            Repeater {
                model: view.decoded.fields
                delegate: Label {
                    required property var modelData
                    required property int index
                    Layout.row: index
                    Layout.column: 1
                    text: modelData.value
                    color: Theme.textPrimary
                    font.family: Type.fixedFamily
                    Accessible.ignored: true
                }
            }
            Repeater {
                model: view.decoded.fields
                delegate: Label {
                    required property var modelData
                    required property int index
                    Layout.row: index
                    Layout.column: 2
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: modelData.meaning
                    font.pointSize: Type.caption
                    Accessible.ignored: true
                    // The one that matters most: limits bound to another descriptor.
                    color: modelData.warn ? Theme.warning : Theme.textMuted
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
