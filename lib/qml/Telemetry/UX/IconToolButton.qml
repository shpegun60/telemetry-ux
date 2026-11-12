// A toolbar action drawn as a pictogram: the icon alone in a 32 px target,
// its name and shortcut in a tooltip, the name to a screen reader, the focus
// frame of the lists. A click leaves the keyboard where it was - typing in
// the file goes on after Undo or Save - and Tab still reaches it. MIT.
//
// `iconName` is a ToolIcon's name. `text` names the action; `shortcutText`,
// when there is one, follows it in the tooltip. `switchedOn` draws the action as
// switched on - filled with the selection colour, as a checked segment is -
// without making the button checkable, so the state stays the caller's.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

Basic.ToolButton {
    id: tool
    property string iconName
    property string shortcutText
    property bool switchedOn: false
    implicitWidth: Type.controlHeight
    implicitHeight: Type.controlHeight
    padding: Type.spacing
    focusPolicy: Qt.TabFocus
    display: AbstractButton.IconOnly
    contentItem: ToolIcon {
        name: tool.iconName
        color: !tool.enabled ? Theme.textDisabled : tool.switchedOn ? Theme.selectedForeground : Theme.textPrimary
    }
    background: Rectangle {
        radius: 4
        color: tool.switchedOn ? Theme.selectionFill
             : tool.down ? Theme.statePressed : tool.hovered ? Theme.stateHover : "transparent"
        border.width: tool.visualFocus ? 2 : 0
        border.color: Theme.focusRing
    }
    ToolTip.visible: (hovered || visualFocus) && text.length > 0
    ToolTip.text: shortcutText.length > 0 ? qsTr("%1 (%2)").arg(text).arg(shortcutText) : text
    Accessible.role: Accessible.Button
    Accessible.name: text
    // A switch (Edit): a screen reader says it is one, and whether it is on.
    property bool toggles: false
    Accessible.checkable: toggles
    Accessible.checked: switchedOn
}
