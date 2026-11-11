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

    readonly property var sourceIndex: treeView.modelIndex(Qt.point(0, row.row))
    readonly property bool container: kind === "category" || kind === "group"
    readonly property color textColor: highlighted ? Theme.selectedForeground : Theme.textPrimary
    readonly property color detailColor: highlighted ? Theme.selectedForeground : Theme.textSecondary

    signal contextMenuRequested(var index, real x, real y)

    implicitWidth: treeView.width
    implicitHeight: Type.rowHeight
    rightPadding: 8
    text: name
    highlighted: current

    Accessible.name: container ? qsTr("%1, %2").arg(name).arg(countText)
                               : access.length > 0 ? qsTr("%1, %2, %3").arg(name).arg(typeText).arg(access)
                               : qsTr("%1, %2").arg(name).arg(typeText)

    contentItem: RowLayout {
        spacing: 8
        Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            elide: Text.ElideRight
            text: row.name
            color: row.textColor
            font.weight: row.kind === "category" ? Type.headingWeight : Font.Normal
        }
        Label {
            objectName: "descriptorRowType"
            Layout.preferredWidth: row.typeWidth
            elide: Text.ElideRight
            textFormat: Text.PlainText
            font.family: Type.fixedFamily
            font.pointSize: Type.caption
            color: row.detailColor
            text: row.typeText
        }
        Label {
            objectName: "descriptorRowAccess"
            visible: row.accessShown
            Layout.preferredWidth: row.accessWidth
            horizontalAlignment: row.container ? Text.AlignRight : Text.AlignLeft
            elide: Text.ElideRight
            font.pointSize: Type.caption
            color: row.container ? (row.highlighted ? Theme.selectedForeground : Theme.textMuted) : row.detailColor
            text: row.container ? row.countText : row.access
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
        onTapped: function(point) { row.contextMenuRequested(row.sourceIndex, point.position.x, point.position.y) }
    }
}
