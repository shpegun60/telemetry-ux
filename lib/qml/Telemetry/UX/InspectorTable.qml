// The hex view's inspector: the bytes at the cursor read as each type, in the
// order chosen with Little-endian / Big-endian. Values in the fixed font,
// right-aligned, plain digits; a row past the end of the file reads "—". MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: inspector
    objectName: "hexInspector"
    padding: 8

    required property var view

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: qsTr("Inspector")
                color: Theme.textPrimary
                font.weight: Type.headingWeight
            }
            ButtonGroup { id: order }
            Button {
                objectName: "hexLittleEndian"
                text: qsTr("Little-endian")
                checkable: true
                checked: !inspector.view.bigEndian
                flat: !checked
                ButtonGroup.group: order
                onClicked: inspector.view.bigEndian = false
            }
            Button {
                objectName: "hexBigEndian"
                text: qsTr("Big-endian")
                checkable: true
                checked: inspector.view.bigEndian
                flat: !checked
                ButtonGroup.group: order
                onClicked: inspector.view.bigEndian = true
            }
        }

        ListView {
            id: rows
            objectName: "hexInspectorRows"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: inspector.view.inspector
            activeFocusOnTab: true
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            highlight: Rectangle {
                color: "transparent"
                border.width: rows.activeFocus ? 2 : 0
                border.color: Theme.focusRing
                radius: 3
            }
            delegate: Item {
                id: row
                required property var modelData
                width: ListView.view.width
                implicitHeight: Type.rowHeight * 0.75
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1: %2").arg(row.modelData.type).arg(row.modelData.value)
                Accessible.description: row.modelData.description
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    Label {
                        Layout.preferredWidth: 80
                        text: row.modelData.type
                        color: Theme.textSecondary
                        font.pointSize: Type.caption
                    }
                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideLeft
                        text: row.modelData.value
                        color: row.modelData.value === "—" ? Theme.textMuted : Theme.textPrimary
                        font.family: Type.fixedFamily
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }
}
