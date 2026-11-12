// One row of the Device tree: a watch box where the Watchlist can hold what
// the row stands for, and the endpoint's or group's name. The row is the
// type scale's row height; the box is 16 px inside a 24 px slot that every
// row keeps, so names line up whether a row has a box or not. The current row
// is framed while the tree has the keyboard. MIT.
//
// The Basic style is imported here only, where the row draws its own box;
// the panel keeps the style the host chose. Colours come from the panel's
// palette, so a host that restyles the panel restyles its rows; a box that is
// off for now takes the Theme's disabled roles.
import QtQuick
import QtQuick.Controls.Basic

TreeViewDelegate {
    id: treeRow
    objectName: "endpointRow"

    required property string name
    required property string detail
    required property bool isEndpoint

    // What the box shows and whether it takes a click; the panel decides.
    property bool watchable: false
    property int watchState: Qt.Unchecked
    property bool watchEnabled: true
    property string watchName: ""
    // Said on the box while it is off.
    property string watchOffReason: ""
    // The row's tooltip; the detail by default.
    property string hint: detail

    // The device's index of this row, whatever is current or selected. Asked
    // again whenever the row may show another node: opening or closing a
    // branch moves nodes to other rows while a row keeps its number, and a
    // reused row takes another node's roles.
    readonly property var sourceIndex: {
        void treeView.rows
        void treeRow.name
        void treeRow.depth
        void treeRow.detail
        return treeView.modelIndex(Qt.point(0, treeRow.row))
    }
    readonly property color textColor: highlighted ? palette.highlightedText : palette.buttonText
    // The slot the box sits in, kept on every row.
    readonly property int boxSlot: Type.rowHeight

    signal watchToggled()
    // A right click on the row: the panel opens its menu there.
    signal contextMenuRequested(real x, real y)

    implicitWidth: treeView.width
    implicitHeight: Type.rowHeight
    indentation: Type.indent
    // The panel's padding is the margin; Device files' rows line up with these.
    leftMargin: 0
    indicator: TreeChevron { row: treeRow }
    text: name

    contentItem: Label {
        leftPadding: treeRow.boxSlot + 2
        textFormat: Text.PlainText
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        text: treeRow.name
        color: treeRow.textColor

        RowCheckBox {
            id: watchBox
            objectName: "endpointWatchCheck"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            visible: treeRow.watchable
            enabled: treeRow.watchEnabled
            tristate: true
            checkState: treeRow.watchState
            // The provider decides; the box shows what it decided.
            nextCheckState: function() { return treeRow.watchState }
            onClicked: {
                treeRow.watchToggled()
                checkState = Qt.binding(function() { return treeRow.watchState })
            }
            Accessible.name: treeRow.watchName
            Accessible.description: watchBox.enabled ? "" : treeRow.watchOffReason
            edgeColor: treeRow.textColor
            indicator.objectName: "endpointWatchBox"
            HoverHandler { id: boxHover }
            PlainToolTip {
                visible: boxHover.hovered && !watchBox.enabled && treeRow.watchOffReason.length > 0
                text: treeRow.watchOffReason
                y: treeRow.height + 2
                delay: 700
            }
        }
    }

    Rectangle {
        objectName: "endpointFocusIndicator"
        anchors.fill: parent
        color: "transparent"
        border.color: treeRow.highlighted ? treeRow.palette.highlightedText : treeRow.palette.text
        border.width: 2
        radius: 3
        visible: treeRow.treeView.activeFocus && treeRow.current
    }
    // Under the row it describes, not over it.
    PlainToolTip {
        objectName: "endpointToolTip"
        visible: treeRow.hovered && treeRow.hint.length > 0
        text: treeRow.hint
        y: treeRow.height + 2
        delay: 700
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(point) { treeRow.contextMenuRequested(point.position.x, point.position.y) }
    }
}
