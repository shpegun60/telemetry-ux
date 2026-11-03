// Shared metadata tooltip; text stays plain. MIT.
import QtQuick
import QtQuick.Controls

ToolTip {
    id: plainToolTip
    contentItem: Label {
        textFormat: Text.PlainText
        text: plainToolTip.text
        font: plainToolTip.font
        wrapMode: Text.Wrap
        color: plainToolTip.palette.toolTipText
    }
}
