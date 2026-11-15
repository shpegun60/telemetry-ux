// A toolbar action drawn as a pictogram: the icon alone in a 32 px target,
// its name and shortcut in a tooltip, the name to a screen reader, the focus
// frame of the lists. A click leaves the keyboard where it was - typing in
// the file goes on after Undo or Save - and Tab still reaches it. MIT.
//
// `iconName` is a ToolIcon's name. `text` names the action; `shortcutText`,
// when there is one, follows it in the tooltip. `switchedOn` draws the action as
// switched on - filled with the selection colour, as a checked segment is -
// without making the button checkable, so the state stays the caller's.
// `disabledReason` says in a few words why the action cannot run now ("not
// connected"): while the button is disabled it follows the name in brackets in
// the tooltip, in place of the shortcut, and is the button's accessible
// description. A disabled control still gets hover in Qt 6, so the tooltip
// shows; it is not drawn as hovered, which would look like it can be pressed.
// `note` is a second clause the tooltip and the accessible description add -
// what the action does not do ("Undo changes only the copy on this
// computer.") - while the name stays short.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic

Basic.ToolButton {
    id: tool
    property string iconName
    property string shortcutText
    property string disabledReason
    property string note
    property bool switchedOn: false
    // The reason is said: disabled, with one given.
    readonly property bool reasonShown: !enabled && disabledReason.length > 0
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
             : tool.down ? Theme.statePressed : tool.hovered && tool.enabled ? Theme.stateHover : "transparent"
        border.width: tool.visualFocus ? 2 : 0
        border.color: Theme.focusRing
    }
    ToolTip.visible: (hovered || visualFocus) && text.length > 0
    ToolTip.text: reasonShown ? qsTr("%1 (%2)").arg(text).arg(disabledReason)
                : (shortcutText.length > 0 ? qsTr("%1 (%2)").arg(text).arg(shortcutText) : text)
                  + (note.length > 0 ? " " + note : "")
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.description: reasonShown ? disabledReason : note
    // A switch (Edit): a screen reader says it is one, and whether it is on.
    property bool toggles: false
    Accessible.checkable: toggles
    Accessible.checked: switchedOn
}
