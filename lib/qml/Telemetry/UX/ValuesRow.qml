// One row of the Values tree: the name, the value and the type in fixed
// columns; a group carries its Field count where a value would be. A value
// that changed in the latest snapshot is marked with a dot and the accent
// colour; a Field the device could not give says so. A large array shows
// its elements 256 at a time, and a "Show 256 more · 256 of 600 shown" row -
// a link - brings the next ones (click, Enter, Space or a screen reader's press). The current row is
// highlighted, and framed while the tree has the keyboard. MIT.
//
// The Basic style is imported here only, where the row customises its look.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

TreeViewDelegate {
    id: row
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

    property real valueWidth: 200
    property real typeWidth: 140
    property bool typeShown: true

    readonly property bool group: kind === "group"
    readonly property bool more: kind === "more"
    // A struct's or array's one-line summary while folded; open, its members say it.
    readonly property string fieldsText: count === 1 ? qsTr("1 Field") : qsTr("%L1 Fields").arg(count)
    // A folded group still says how many of its Fields the latest snapshot changed.
    readonly property string valueText: group ? (changedFields > 0 ? qsTr("%1 · %L2 changed").arg(fieldsText).arg(changedFields)
                                                                   : fieldsText)
                                      : hasChildren && expanded ? "" : value
    readonly property color textColor: highlighted ? Theme.selectedForeground : Theme.textPrimary
    readonly property color detailColor: highlighted ? Theme.selectedForeground : Theme.textSecondary
    readonly property color valueColor: highlighted ? Theme.selectedForeground
                                      : group || unavailable ? Theme.textMuted
                                      : changed ? Theme.accent : Theme.textPrimary

    signal contextMenuRequested(var index, real x, real y)
    signal moreRequested(var index)

    implicitWidth: treeView.width
    implicitHeight: Type.rowHeight
    rightPadding: 8
    text: name
    highlighted: current

    Accessible.role: more ? Accessible.Button : Accessible.TreeItem
    // A screen reader's or voice control's press brings the next elements as a click does.
    Accessible.onPressAction: if (row.more) row.moreRequested(row.treeView.index(row.row, 0))
    Accessible.name: more ? name
                   : group ? qsTr("%1, %2").arg(name).arg(valueText)
                   : unavailable ? qsTr("%1, unavailable, %2").arg(name).arg(typeText)
                   : value.length === 0 ? qsTr("%1, %2").arg(name).arg(typeText)
                   : changed ? qsTr("%1, %2, changed, %3").arg(name).arg(value).arg(typeText)
                   : qsTr("%1, %2, %3").arg(name).arg(value).arg(typeText)

    contentItem: RowLayout {
        spacing: 8
        Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            elide: Text.ElideRight
            text: row.name
            // A "more" row is a link as LinkButton draws one: secondary text,
            // always underlined; the accent stays the changed values' mark.
            color: row.more ? row.detailColor : row.textColor
            font.weight: row.group ? Type.headingWeight : Font.Normal
            font.underline: row.more
            HoverHandler {
                id: moreHover
                enabled: row.more
                cursorShape: Qt.PointingHandCursor
            }
        }
        // The value, with the changed mark 4 px before its first character.
        // A "more" row has no value or type: its words take the whole row.
        Item {
            visible: !row.more
            Layout.preferredWidth: 10 + row.valueWidth
            Layout.fillHeight: true
            Rectangle {
                width: 6
                height: 6
                radius: 3
                anchors.verticalCenter: parent.verticalCenter
                x: valueLabel.x + Math.max(0, valueLabel.width - valueLabel.contentWidth) *
                   (valueLabel.horizontalAlignment === Text.AlignRight ? 1 : 0) - width - 4
                visible: (row.group ? row.changedFields > 0 : row.changed) && !row.highlighted
                color: Theme.accent
                Accessible.ignored: true
            }
            Label {
                id: valueLabel
                objectName: "valuesRowValue"
                x: 10
                width: row.valueWidth
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
        Item { visible: !row.more; Layout.preferredWidth: 8 }
        Label {
            objectName: "valuesRowType"
            visible: row.typeShown && !row.more
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
        enabled: row.more
        onTapped: row.moreRequested(row.treeView.index(row.row, 0))
    }
    TapHandler {
        enabled: !row.more
        acceptedButtons: Qt.RightButton
        // The index of this row now: a delegate is reused for other rows.
        onTapped: function(point) { row.contextMenuRequested(row.treeView.index(row.row, 0), point.position.x, point.position.y) }
    }
}
