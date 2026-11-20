// The Watchlist of WL-9 (the designer's of 20 November 2026) over the fixture
// of its section F: the demo device and the enum device.Mode, a fake provider
// (WatchLook.cpp) that refuses Gain above 10, answers when told and counts
// reads. A real UndoHistory and router, as a host's. MIT.
import QtQuick
import QtQuick.Controls
import Telemetry.UX

ApplicationWindow {
    id: win
    width: 560
    height: 520
    visible: true
    color: Theme.surfaceWindow
    UndoHistory { id: workspaceHistory; objectName: "workspaceHistory" }
    UndoRouter { objectName: "undoRouter"; workspace: workspaceHistory }
    Component.onCompleted: lookProvider.history = workspaceHistory
    WatchlistPanel {
        id: panel
        objectName: "lookPanel"
        anchors.fill: parent
        provider: lookProvider
        collapsible: false
        framed: false
    }
    function setBody(points) { Type.body = points }
    function body() { return Type.body }
    function token(name) { return Type[name] }
    function color(name) { return String(Theme[name]) }
    function reduce(on) { panel.reducedMotion = on }
}
