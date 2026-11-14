// One row of the Values tree, and of the JSON tree: the name, the value and
// the type in fixed columns; a group carries its Field count where a value
// would be. A value that changed in the latest snapshot is marked with a
// 2 px line in the info colour and drawn in it - not a dot, which reads as the
// Watchlist's OK; a Field the device could not give says so. A long
// array opens as ranges of 256, "[0 … 255]", "[256 … 511]", … - as Eclipse
// shows one - and a range opens like any row. The current row is
// highlighted, and framed while the tree has the keyboard. MIT.
//
// The Basic style is imported here only, where the row customises its look.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

TreeViewDelegate {
    id: row
    indentation: Type.indent
    indicator: TreeChevron { row: row }
    objectName: "valuesRow"

    required property string name
    required property string kind
    required property string typeText
    required property string value
    required property bool unavailable
    required property bool changed
    required property int count
    required property bool numeric
    required property int changedFields
    // The Field's access, letter by letter; empty on other rows.
    property string access: ""
    property real accessWidth: 0

    property real valueWidth: 200
    property real typeWidth: 140
    property bool typeShown: true

    readonly property bool group: kind === "group"
    // A struct's or array's one-line summary while folded; open, its members say it.
    readonly property string fieldsText: count === 1 ? qsTr("1 Field") : qsTr("%L1 Fields").arg(count)
    // A folded group still says how many of its Fields the latest snapshot changed.
    readonly property string valueText: group ? (changedFields > 0 ? qsTr("%1 · %L2 changed").arg(fieldsText).arg(changedFields)
                                                                   : fieldsText)
                                      : hasChildren && expanded ? "" : value
    readonly property color textColor: highlighted ? Theme.selectedForeground : Theme.textPrimary
    readonly property color detailColor: highlighted ? Theme.selectedForeground : Theme.textSecondary
    readonly property color valueColor: highlighted ? Theme.selectedForeground
                                      : group ? (changedFields > 0 ? Theme.info : Theme.textMuted)
                                      : unavailable ? Theme.textMuted
                                      : changed ? Theme.info : Theme.textPrimary

    signal contextMenuRequested(var index, real x, real y)

    implicitWidth: treeView.width
    implicitHeight: Type.rowHeight
    rightPadding: Type.padding
    text: name
    highlighted: current

    Accessible.role: Accessible.TreeItem
    Accessible.name: kind === "range" ? qsTr("%1, %2").arg(name.replace("…", qsTr("to"))).arg(typeText)
                   : group ? qsTr("%1, %2").arg(name).arg(valueText)
                   : unavailable ? qsTr("%1, unavailable, %2").arg(name).arg(typeText)
                   : value.length === 0 ? qsTr("%1, %2").arg(name).arg(typeText)
                   : changed ? qsTr("%1, %2, changed, %3").arg(name).arg(value).arg(typeText)
                   : qsTr("%1, %2, %3").arg(name).arg(value).arg(typeText)

    contentItem: RowLayout {
        spacing: Type.padding
        Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            elide: Text.ElideRight
            text: row.name
            color: row.textColor
            font.weight: row.group ? Type.headingWeight : Font.Normal
        }
        // The value; a changed one is drawn in the info colour. A group's
        // count spans Value, Type and rw, to the row's right edge.
        Item {
            readonly property real groupExtra: 8 + (row.typeShown ? row.typeWidth + Type.padding : 0)
                                              + (row.accessWidth > 0 ? row.accessWidth + Type.padding : 0)
            Layout.preferredWidth: 10 + row.valueWidth + (row.group ? groupExtra : 0)
            Layout.fillHeight: true
            Label {
                id: valueLabel
                objectName: "valuesRowValue"
                x: 10
                width: parent.width - 10
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                textFormat: Text.PlainText
                font.family: row.group ? Qt.application.font.family : Type.fixedFamily
                font.pointSize: row.group ? Type.caption : Type.body
                font.italic: row.unavailable
                horizontalAlignment: row.group || row.numeric ? Text.AlignRight : Text.AlignLeft
                color: row.valueColor
                text: row.valueText
                HoverHandler { id: valueHover }
                ToolTip.visible: valueHover.hovered && truncated
                ToolTip.text: row.valueText
            }
        }
        // A wider gap: "230 f32" must not read as one word.
        Item { visible: !row.group; Layout.preferredWidth: 8 }
        Label {
            objectName: "valuesRowType"
            visible: row.typeShown && !row.group
            Layout.preferredWidth: row.typeWidth
            elide: Text.ElideRight
            textFormat: Text.PlainText
            font.family: Type.fixedFamily
            font.pointSize: Type.caption
            color: row.detailColor
            text: row.typeText
            HoverHandler { id: typeHover }
            ToolTip.visible: typeHover.hovered && truncated
            ToolTip.text: row.typeText
        }
        Row {
            objectName: "valuesRowAccess"
            visible: row.accessWidth > 0 && !row.group
            Layout.preferredWidth: row.accessWidth
            readonly property string text: row.access
            Repeater {
                model: row.access.length
                Label {
                    required property int index
                    text: row.access.charAt(index)
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    color: row.highlighted ? Theme.selectedForeground : text === "-" ? Theme.textDisabled : Theme.textSecondary
                }
            }
        }
    }

    // A changed Field's mark: a line the row's height along its left edge,
    // not before the digits, where it read as a text cursor.
    Rectangle {
        objectName: "valuesChangeMark"
        x: 0
        width: 2
        height: row.height
        color: Theme.info
        visible: !row.group && row.changed && !row.highlighted
        Accessible.ignored: true
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: 3
        border.width: 2
        border.color: Theme.focusRing
        visible: row.treeView.activeFocus && row.current
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        // The index of this row now: a delegate is reused for other rows.
        onTapped: function(point) { row.contextMenuRequested(row.treeView.index(row.row, 0), point.position.x, point.position.y) }
    }
}
