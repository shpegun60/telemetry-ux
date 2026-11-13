// A tree row's expand chevron, sized from the type scale: an indent wide, at
// the row's depth, turned down while the branch is open. Every tree in the
// module uses it as its delegate's indicator, so a zoom scales it with the
// text. MIT.
import QtQuick

Item {
    id: chevron
    // The TreeViewDelegate it belongs to.
    required property var row
    readonly property real indent: row.leftMargin + row.depth * row.indentation
    x: !row.mirrored ? indent : row.width - indent - width
    y: Math.round((row.height - height) / 2)
    implicitWidth: Type.indent
    // A row high, so the whole height opens the branch, as Basic's did.
    implicitHeight: row.height
    Accessible.ignored: true
    Chevron {
        anchors.centerIn: parent
        width: Type.iconSize
        height: Type.iconSize
        open: chevron.row.expanded
        mirrored: chevron.row.mirrored
        color: chevron.row.highlighted ? chevron.row.palette.highlightedText : Theme.textSecondary
    }
}
