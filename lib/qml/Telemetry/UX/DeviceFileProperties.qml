// A file's properties beside its row (Alt+Enter or the menu): its device path
// with Copy, its size exactly and in units, its access. Non-modal; Esc closes
// it and the keyboard goes back to the row. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: properties
    objectName: "deviceFileProperties"
    padding: 12
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    required property var files
    property var file: ({})

    signal closedBack()

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.outline
        radius: 4
    }

    contentItem: GridLayout {
        // A popup is not an item: its content carries the dialog's role.
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Properties of %1").arg(properties.file.name || "")
        columns: 2
        columnSpacing: 12
        rowSpacing: 6

        Label {
            Layout.columnSpan: 2
            text: properties.file.name || ""
            color: Theme.textPrimary
            font.weight: Type.headingWeight
            textFormat: Text.PlainText
        }
        Label { text: qsTr("Path"); color: Theme.textSecondary; font.pointSize: Type.caption }
        RowLayout {
            spacing: 8
            Label {
                objectName: "deviceFilePropertiesPath"
                Layout.maximumWidth: 360
                elide: Text.ElideMiddle
                textFormat: Text.PlainText
                font.family: Type.fixedFamily
                color: Theme.textPrimary
                text: properties.file.path || ""
            }
            LinkButton {
                id: copyLink
                objectName: "deviceFilePropertiesCopy"
                tint: Theme.textSecondary
                text: qsTr("Copy")
                Accessible.name: qsTr("Copy the path")
                onClicked: Clipboard.setText(properties.file.path || "")
            }
        }
        Label { text: qsTr("Size"); color: Theme.textSecondary; font.pointSize: Type.caption }
        Label {
            objectName: "deviceFilePropertiesSize"
            color: Theme.textPrimary
            text: properties.file.sizeKnown === false ? qsTr("not known yet")
                : ((properties.file.size || 0) === 1 ? qsTr("1 byte")
                   : qsTr("%1 bytes").arg(Number(properties.file.size || 0).toLocaleString(Qt.locale(), "f", 0))) +
                  ((properties.file.size || 0) >= 1024 ? qsTr(" (%1)").arg(properties.files.formatSize(properties.file.size)) : "")
        }
        Label { text: qsTr("Access"); color: Theme.textSecondary; font.pointSize: Type.caption }
        Label {
            objectName: "deviceFilePropertiesAccess"
            color: Theme.textPrimary
            // The Access column's own words.
            text: properties.file.access || "" 
        }
    }

    onOpened: copyLink.forceActiveFocus()
    onClosed: properties.closedBack()
}
