// What a panel sees of the Theme and Type singletons. MIT.
import QtQuick
import Telemetry.UX

QtObject {
    objectName: "themeTypes"
    readonly property real body: Type.body
    readonly property real caption: Type.caption
    readonly property real heading: Type.heading
    readonly property int rowHeight: Type.rowHeight
    readonly property color surfaceWindow: Theme.surfaceWindow
    readonly property color muted: Theme.textMuted
    property color accentBefore
    readonly property color accentAfter: Theme.accent
    Component.onCompleted: accentBefore = Theme.accent
    // A host restyles by assigning a role.
    function restyle() { Theme.accent = "#ff8800" }
}
