// One row of the Device files tree: the device's own name, the size and the
// access in fixed columns. A selected row is highlighted - a folder stands for
// its files, and one with some of them selected carries a line along its
// edge; the current row is framed while the tree has the keyboard. A click is
// the panel's to read with its keys (Ctrl, Shift). MIT.
//
// The Basic style is imported here only, where the row customises its
// controls; the panel keeps the style the host chose.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

TreeViewDelegate {
    id: fileRow
    indentation: Type.indent
    // The panel has no padding: the margin lines the chevrons up with the
    // Device tree's.
    leftMargin: Type.padding
    indicator: TreeChevron { row: fileRow }
    objectName: "deviceFileRow"

    required property string name
    required property bool isFolder
    required property bool markable
    required property string sizeText
    required property string spokenSize
    required property string accessText
    required property int marked

    // The panel's column widths, so every row and the header line up.
    property real sizeWidth: 72
    property real accessWidth: 72
    property bool accessShown: true

    // The device's index of this row, whatever is current or marked.
    readonly property var sourceIndex: treeView.modelIndex(Qt.point(0, row))
    readonly property color textColor: highlighted ? Theme.selectedForeground : Theme.textPrimary
    readonly property color detailColor: highlighted ? Theme.selectedForeground : Theme.textSecondary

    // A left click, with the keys held: the panel selects from it.
    signal rowClicked(var index, int row, int modifiers)
    signal openRequested(var index)
    // A right click on the row: the panel opens its menu there.
    signal contextMenuRequested(var index, real x, real y)

    implicitWidth: treeView.width
    implicitHeight: Type.rowHeight
    rightPadding: Type.padding
    text: name
    highlighted: marked === Qt.Checked

    // The device's name goes in last, under the highest number, so nothing in
    // it is ever read as a placeholder.
    Accessible.name: isFolder ? qsTr("%2, folder, %1").arg(spokenSize).arg(name)
                              : qsTr("%3, %1, %2").arg(spokenSize).arg(accessText).arg(name)
    Accessible.selectable: markable
    Accessible.selected: marked === Qt.Checked

    contentItem: RowLayout {
        spacing: Type.padding

        // The device's own name, never replaced.
        Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            text: fileRow.name
            color: fileRow.textColor
        }
        Label {
            objectName: "deviceFileSize"
            Layout.preferredWidth: fileRow.sizeWidth
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            font.pointSize: Type.caption
            color: fileRow.detailColor
            text: fileRow.sizeText
        }
        Label {
            objectName: "deviceFileAccess"
            visible: fileRow.accessShown
            Layout.preferredWidth: fileRow.accessWidth
            elide: Text.ElideRight
            font.pointSize: Type.caption
            color: fileRow.detailColor
            text: fileRow.accessText
        }
    }

    // Some of a folder's files selected, not all: a line along its edge.
    Rectangle {
        objectName: "deviceFilePartlySelected"
        x: 0
        width: 2
        height: fileRow.height
        color: Theme.selectionFill
        visible: fileRow.isFolder && fileRow.marked === Qt.PartiallyChecked
        Accessible.ignored: true
    }

    Rectangle {
        objectName: "deviceFileFocusFrame"
        anchors.fill: parent
        color: "transparent"
        radius: 3
        border.width: 2
        border.color: Theme.focusRing
        visible: fileRow.treeView.activeFocus && fileRow.current
    }

    onDoubleClicked: if (!isFolder) fileRow.openRequested(sourceIndex)
    // One handler per set of keys held: the tap's own point carries no keys.
    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.NoModifier
        onTapped: fileRow.rowClicked(fileRow.sourceIndex, fileRow.row, Qt.NoModifier)
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.ControlModifier
        onTapped: fileRow.rowClicked(fileRow.sourceIndex, fileRow.row, Qt.ControlModifier)
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.ShiftModifier
        onTapped: fileRow.rowClicked(fileRow.sourceIndex, fileRow.row, Qt.ShiftModifier)
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.ShiftModifier | Qt.ControlModifier
        onTapped: fileRow.rowClicked(fileRow.sourceIndex, fileRow.row, Qt.ShiftModifier | Qt.ControlModifier)
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(point) {
            fileRow.contextMenuRequested(fileRow.sourceIndex, point.position.x, point.position.y)
        }
    }
}
