// Telemetry's own compact tab style, for the local docking experiment.
import QtQuick
import QtQuick.Templates as T

T.TabButton {
    id: tab
    implicitWidth: Math.max(110, label.implicitWidth + 32)
    implicitHeight: 40
    padding: 8
    font.pixelSize: 14
    font.weight: checked ? Font.DemiBold : Font.Normal
    contentItem: Text {
        id: label
        text: tab.text
        font: tab.font
        color: !tab.enabled ? "#718297" : tab.checked ? "#f3f7fa" : "#b8c9d8"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        color: tab.checked ? "#263b4c" : tab.hovered ? "#203244" : "#172331"
        border.color: tab.visualFocus ? "#8ce6d3" : "#34495a"
        border.width: 1
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 3
            color: "#83e1ce"
            visible: tab.checked
        }
    }
}
