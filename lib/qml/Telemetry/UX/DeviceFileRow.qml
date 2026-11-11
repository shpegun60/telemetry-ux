// One row of the Device files tree: its mark, the device's own name, the size
// and the access in fixed columns. The current row is highlighted, and framed
// while the tree has the keyboard. MIT.
//
// The Basic style is imported here only, where the row customises its
// controls; the panel keeps the style the host chose.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

TreeViewDelegate {
    id: fileRow
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

    signal markRequested(var index, bool value)
    signal openRequested(var index)

    implicitWidth: treeView.width
    implicitHeight: Type.rowHeight
    rightPadding: 8
    text: name
    highlighted: current

    // The device's name goes in last, under the highest number, so nothing in
    // it is ever read as a placeholder.
    Accessible.name: isFolder ? qsTr("%2, folder, %1").arg(spokenSize).arg(name)
                              : qsTr("%3, %1, %2").arg(spokenSize).arg(accessText).arg(name)
    Accessible.checkable: markable
    Accessible.checked: marked === Qt.Checked

    contentItem: RowLayout {
        spacing: 8

        CheckBox {
            objectName: "deviceFileMark"
            focusPolicy: Qt.NoFocus
            enabled: fileRow.markable
            tristate: fileRow.isFolder
            checkState: fileRow.marked
            // A click marks or unmarks; it never stops on "partly marked".
            nextCheckState: function() { return fileRow.marked === Qt.Checked ? Qt.Unchecked : Qt.Checked }
            Accessible.name: qsTr("Mark %1 for download").arg(fileRow.name)
            onClicked: {
                fileRow.markRequested(fileRow.sourceIndex, checkState === Qt.Checked)
                // The model decides; the box shows what it decided.
                checkState = Qt.binding(function() { return fileRow.marked })
            }
        }
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
}
