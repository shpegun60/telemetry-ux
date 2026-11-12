// What a panel sees of the Theme and Type singletons. MIT.
import QtQuick
import Telemetry.UX

QtObject {
    objectName: "themeTypes"
    readonly property real body: Type.body
    readonly property real caption: Type.caption
    readonly property real heading: Type.heading
    readonly property int rowHeight: Type.rowHeight
    readonly property int controlHeight: Type.controlHeight
    readonly property int chipHeight: Type.chipHeight
    readonly property int padding: Type.padding
    readonly property int spacing: Type.spacing
    readonly property int groupSpacing: Type.groupSpacing
    readonly property int tight: Type.tight
    readonly property int iconSize: Type.iconSize
    readonly property int checkSize: Type.checkSize
    readonly property real bodyPixels: Type.bodyPixels
    readonly property color surfaceWindow: Theme.surfaceWindow
    readonly property color muted: Theme.textMuted
    property color accentBefore
    readonly property color accentAfter: Theme.accent
    Component.onCompleted: accentBefore = Theme.accent
    // A host restyles by assigning a role.
    function restyle() { Theme.accent = "#ff8800" }
    // A host whose controls take another font sets the body; the steps follow.
    function setBody(points) { Type.body = points }
}
