// A title that folds what is under it: a chevron, the title in the heading
// weight and a muted count, checked while open. The Watchlist and Batch fold
// with it, so both read alike; pure QML, a control high. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

T.AbstractButton {
    id: fold
    // The muted figure after the title: how many, or "3/32".
    property string countText: ""
    checkable: true
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    leftPadding: Type.tight
    rightPadding: Type.spacing
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Type.controlHeight
    contentItem: Row {
        spacing: Type.tight
        Chevron {
            anchors.verticalCenter: parent.verticalCenter
            open: fold.checked
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: fold.text
            font.weight: Type.headingWeight
            color: fold.enabled ? Theme.textPrimary : Theme.textDisabled
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            visible: fold.countText.length > 0
            leftPadding: Type.tight
            text: fold.countText
            color: Theme.textMuted
            font.pointSize: Type.caption
        }
    }
    background: Rectangle {
        radius: Type.radius
        color: fold.down ? Theme.statePressed : fold.hovered ? Theme.stateHover : "transparent"
        border.width: fold.visualFocus ? 2 : 0
        border.color: Theme.focusRing
    }
    Accessible.role: Accessible.CheckBox
    Accessible.name: countText.length > 0 ? qsTr("%1, %2").arg(text).arg(countText) : text
    Accessible.checkable: true
    Accessible.checked: checked
}
