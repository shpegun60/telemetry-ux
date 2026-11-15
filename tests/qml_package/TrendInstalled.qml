import QtQuick
import QtQuick.Window
import Telemetry.UX
Window {
    width: 640; height: 400; visible: true
    TrendPanel { anchors.fill: parent; provider: testProvider; history: testHistory }
    // The central Undo's history, made as a host makes one (UR-1), with the
    // window's router, its toolbar pair, a notice and a scope.
    UndoHistory { id: workspace; objectName: "workspaceHistory" }
    UndoRouter { id: router; objectName: "undoRouter"; workspace: workspace }
    UndoButtons { objectName: "undoButtons"; router: router }
    UndoNotice { objectName: "undoNotice"; history: workspace; y: 40 }
    Item { objectName: "inertArea"; UndoScope.inert: true }
}
