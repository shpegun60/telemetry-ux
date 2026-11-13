// One row of the Device files tree: the device's own name, the size and the
// access in fixed columns. A selected file is highlighted - fully while the
// tree has the keyboard, quietly while it does not. A folder is never
// highlighted: it stands for its files and shows how many of them are
// selected. A file the device does not let us read is dimmed and says why.
// The current row is framed while the tree has the keyboard. A click is the
// panel's to read with its keys (Ctrl, Shift). MIT.
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
    required property bool downloadable
    required property string sizeText
    required property string spokenSize
    required property string accessText
    required property bool isSelected
    required property int selectedInside
    required property int readableInside
    required property int fileCount

    // The panel's column widths, so every row and the header line up.
    property real sizeWidth: 72
    property real accessWidth: 72
    property bool accessShown: true

    // The device's index of this row, whatever is current or selected.
    readonly property var sourceIndex: treeView.modelIndex(Qt.point(0, row))
    readonly property bool focused: treeView.activeFocus
    readonly property bool unreadable: !isFolder && !downloadable
    readonly property color textColor: highlighted ? (focused ? Theme.selectedForeground : Theme.textPrimary)
                                     : unreadable ? Theme.textDisabled : Theme.textPrimary
    readonly property color detailColor: highlighted ? (focused ? Theme.selectedForeground : Theme.textPrimary)
                                       : unreadable ? Theme.textDisabled : Theme.textSecondary
    readonly property string unreadableText: qsTr("Can't be read from the device, so it can't be downloaded")
    readonly property int unreadableInside: fileCount - readableInside
    // "3 of 6 files selected", and why a folder's click left some out.
    readonly property string insideText: (unreadableInside > 0
            ? qsTr("%1 of %2 files selected (%3 can't be read)").arg(selectedInside).arg(fileCount).arg(unreadableInside)
            : qsTr("%1 of %2 files selected").arg(selectedInside).arg(fileCount))

    // A left click, with the keys held: the panel selects from it.
    signal rowClicked(var index, int row, int modifiers)
    signal openRequested(var index)
    // A right click on the row: the panel opens its menu there.
    signal contextMenuRequested(var index, real x, real y)

    implicitWidth: treeView.width
    implicitHeight: Type.rowHeight
    rightPadding: Type.padding
    text: name
    highlighted: !isFolder && isSelected
    palette.highlight: focused ? Theme.stateSelected : Theme.stateSelectedInactive

    ToolTip.visible: unreadable && hovered
    ToolTip.delay: 600
    ToolTip.text: unreadableText

    // The device's name goes in last, under the highest number, so nothing in
    // it is ever read as a placeholder.
    Accessible.name: isFolder ? qsTr("%2, folder, %1").arg(spokenSize).arg(name)
                              : qsTr("%3, %1, %2").arg(spokenSize).arg(accessText).arg(name)
    Accessible.description: unreadable ? unreadableText : isFolder && selectedInside > 0 ? insideText : ""
    Accessible.selectable: !isFolder
    Accessible.selected: highlighted

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
        // A folder holding selected files: how many, in a pill.
        Label {
            id: inside
            objectName: "deviceFileSelectedInside"
            visible: fileRow.isFolder && fileRow.selectedInside > 0
            leftPadding: Type.tight + 1
            rightPadding: Type.tight + 1
            font.pointSize: Type.caption
            color: Theme.selectedForeground
            text: qsTr("%L1").arg(fileRow.selectedInside)
            background: Rectangle {
                radius: height / 2
                color: Theme.selectionFill
            }
            HoverHandler { id: insideHover }
            ToolTip.visible: insideHover.hovered
            ToolTip.delay: 300
            ToolTip.text: fileRow.insideText
            Accessible.ignored: true
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

    // Inset a pixel, so a selected neighbour never swallows its edges.
    Rectangle {
        objectName: "deviceFileFocusFrame"
        anchors.fill: parent
        anchors.margins: 1
        color: "transparent"
        radius: 3
        border.width: 2
        border.color: Theme.focusRing
        visible: fileRow.focused && fileRow.current
    }

    onDoubleClicked: if (!isFolder) fileRow.openRequested(sourceIndex)
    // The handler's own point carries the keys held; the tapped argument
    // does not.
    TapHandler {
        id: leftTap
        acceptedButtons: Qt.LeftButton
        onTapped: fileRow.rowClicked(fileRow.sourceIndex, fileRow.row, leftTap.point.modifiers)
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(point) {
            fileRow.contextMenuRequested(fileRow.sourceIndex, point.position.x, point.position.y)
        }
    }
}
