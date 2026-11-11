// An action drawn as a link that the keyboard reaches too: underlined text, a
// 24 px target, and the focus frame of the lists. MIT.
//
// Used where a full button would be too loud: Cancel on a transfer row, Retry,
// Show in folder. `tint` is the text colour: secondary text by default, since
// the underline already says "link" and the accent belongs to the one primary
// action; an error banner passes errorLink, an info chip infoLink.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

Basic.ToolButton {
    id: link
    property color tint: Theme.textSecondary
    implicitHeight: 24
    leftPadding: 4
    rightPadding: 4
    topPadding: 0
    bottomPadding: 0
    focusPolicy: Qt.StrongFocus
    contentItem: Label {
        text: link.text
        font: link.font
        color: link.tint
        verticalAlignment: Text.AlignVCenter
    }
    font.underline: true
    background: Rectangle {
        radius: 4
        color: link.down ? Theme.statePressed : link.hovered ? Theme.stateHover : "transparent"
        border.width: link.visualFocus ? 2 : 0
        border.color: Theme.focusRing
    }
}
