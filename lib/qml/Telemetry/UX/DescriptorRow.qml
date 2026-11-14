// One row of the Descriptor tree: the name, its type and its access in fixed
// columns; a category or group carries its count where access would be. The
// current row is highlighted, and framed while the tree has the keyboard. MIT.
//
// The Basic style is imported here only, where the row customises its look.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

TreeViewDelegate {
    id: row
    indentation: Type.indent
    indicator: TreeChevron { row: row }
    objectName: "descriptorRow"

    required property string name
    required property string kind
    required property string typeText
    required property string access
    required property int count
    required property int category
    // The count in its category's word, as the header counts.
    readonly property string countText: category === 1 ? (count === 1 ? qsTr("1 Command") : qsTr("%1 Commands").arg(count))
        : category === 2 ? (count === 1 ? qsTr("1 Service") : qsTr("%1 Services").arg(count))
        : (count === 1 ? qsTr("1 Field") : qsTr("%1 Fields").arg(count))

    property real typeWidth: 160
    property real accessWidth: 120
    property bool accessShown: true
    // A category's bare number; a group's count in its kind's word.
    readonly property string headingCount: kind === "category" ? qsTr("%L1").arg(count) : countText

    readonly property bool container: kind === "category" || kind === "group"
    readonly property color textColor: highlighted ? Theme.selectedForeground : Theme.textPrimary
    readonly property color detailColor: highlighted ? Theme.selectedForeground : Theme.textSecondary
    // The access words the model carries, drawn as the Device tree's marks.
    readonly property string mark: access === "read, write" ? "rw" : access === "read" ? "r-"
                                 : access === "write" ? "-w" : access === "no access" ? "--" : ""
    readonly property string accessWords: mark === "rw" ? qsTr("read and write") : mark === "r-" ? qsTr("read only")
                                        : mark === "-w" ? qsTr("write only") : mark === "--" ? qsTr("no access") : ""

    signal contextMenuRequested(var index, real x, real y)

    implicitWidth: treeView.width
    implicitHeight: Type.rowHeight
    rightPadding: Type.padding
    text: name
    highlighted: current

    Accessible.name: container ? qsTr("%1, %2").arg(name).arg(countText)
                               : access.length > 0 ? qsTr("%1, %2, %3").arg(name).arg(typeText).arg(accessWords)
                               : qsTr("%1, %2").arg(name).arg(typeText)

    contentItem: RowLayout {
        spacing: Type.padding
        Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            elide: Text.ElideRight
            text: row.name
            color: row.textColor
            font.weight: row.container ? Type.headingWeight : Font.Normal
        }
        // A container's count stands at the right edge, over the columns it spans.
        Label {
            objectName: "descriptorRowCount"
            visible: row.container
            font.pointSize: Type.caption
            color: row.highlighted ? Theme.selectedForeground : Theme.textMuted
            text: row.headingCount
        }
        Label {
            objectName: "descriptorRowType"
            visible: !row.container
            Layout.preferredWidth: row.typeWidth
            elide: Text.ElideRight
            textFormat: Text.PlainText
            font.family: Type.fixedFamily
            font.pointSize: Type.caption
            color: row.detailColor
            text: row.typeText
        }
        // The Field's access, letter by letter, a dash for what it lacks.
        Row {
            objectName: "descriptorRowAccess"
            visible: row.accessShown && !row.container
            Layout.preferredWidth: row.accessWidth
            readonly property string text: row.mark
            Repeater {
                model: row.mark.length
                Label {
                    required property int index
                    text: row.mark.charAt(index)
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    color: row.highlighted ? Theme.selectedForeground : text === "-" ? Theme.textDisabled : Theme.textSecondary
                }
            }
            HoverHandler { id: accessHover }
            PlainToolTip { visible: accessHover.hovered && row.accessWords.length > 0; text: row.accessWords; delay: 700 }
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
        acceptedButtons: Qt.RightButton
        // The index of this row now: a delegate is reused for other rows.
        onTapped: function(point) { row.contextMenuRequested(row.treeView.index(row.row, 0), point.position.x, point.position.y) }
    }
}
