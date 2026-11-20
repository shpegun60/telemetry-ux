// One kind in the Device tree's kinds filter (the designer's review of 20
// November 2026, 14.1): checkable, the kind's word and, muted after it, the
// device's total of that kind ("Fields  1,204") - the totals the category
// rows used to show. The Basic style is imported here only, where the item
// draws its own text in two tones. MIT.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

MenuItem {
    id: item
    property string word: ""
    // -1 while the model does not say.
    property int total: -1
    readonly property string totalText: total >= 0 ? Number(total).toLocaleString(Qt.locale(), "f", 0) : ""
    checkable: true
    text: totalText.length > 0 ? qsTr("%1, %2").arg(word).arg(totalText) : word
    contentItem: RowLayout {
        spacing: Type.groupSpacing
        // Room for the check mark the style draws at the left.
        Item { Layout.preferredWidth: item.indicator ? item.indicator.width + item.spacing - Type.groupSpacing : 0 }
        Label {
            text: item.word
            font: item.font
            color: item.highlighted ? Theme.selectedForeground : Theme.textPrimary
            Layout.fillWidth: true
        }
        Label {
            objectName: "kindsMenuTotal"
            visible: item.totalText.length > 0
            text: item.totalText
            font.pointSize: Type.caption
            color: item.highlighted ? Theme.selectedForeground : Theme.textMuted
        }
    }
    Accessible.name: text
}
