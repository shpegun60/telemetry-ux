// The check box at the start of a list or tree row: a row-square target with
// an even box drawn in its middle - filled and ticked when marked, a dash when
// partly - never taking the keyboard from the list. The Device tree and Device
// files share it, so both read alike at every zoom. `edgeColor` follows the
// row's text, so a box on a selected row stays visible. MIT.
import QtQuick
import QtQuick.Templates as T

T.CheckBox {
    id: box
    property color edgeColor: palette.windowText
    implicitWidth: Type.rowHeight
    implicitHeight: Type.rowHeight
    padding: 0
    spacing: 0
    focusPolicy: Qt.NoFocus
    contentItem: Item {}
    indicator: Rectangle {
        objectName: "rowCheckBoxMark"
        implicitWidth: Type.checkSize
        implicitHeight: Type.checkSize
        x: Math.round((box.width - width) / 2)
        y: Math.round((box.height - height) / 2)
        radius: Type.radius
        readonly property bool marked: box.checkState !== Qt.Unchecked
        color: !marked ? "transparent" : box.enabled ? box.palette.highlight : Theme.surfaceDisabled
        border.width: 1
        border.color: box.enabled ? box.edgeColor : Theme.textDisabled
        Text {
            anchors.centerIn: parent
            visible: parent.marked
            text: box.checkState === Qt.Checked ? "✓" : "–"
            color: box.enabled ? box.palette.highlightedText : Theme.textDisabled
            font.pointSize: Type.caption
            font.bold: true
        }
    }
}
