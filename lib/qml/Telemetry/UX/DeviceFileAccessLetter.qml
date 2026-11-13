// One slot of a Device files row's access mark: a letter weighs more than a
// dash or a dot; on a file that cannot be read every slot keeps its weight,
// being why the row is dimmed. MIT.
import QtQuick
import QtQuick.Controls

Label {
    required property var row
    property string letter
    readonly property bool marked: letter === "r" || letter === "w" || letter === "s" || letter === "?"

    font.family: Type.fixedFamily
    font.pointSize: Type.caption
    color: row.unreadable ? Theme.textSecondary
         : marked ? (row.strong ? Theme.selectedForeground : Theme.textSecondary)
         : row.strong ? Theme.textSecondary : Theme.textDisabled
    text: letter
    Accessible.ignored: true
}
