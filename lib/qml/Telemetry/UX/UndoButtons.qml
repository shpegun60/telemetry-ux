// Undo and Redo as pictograms for a host's toolbar, over the window's
// UndoRouter (UR-1). MIT.
//
// They act as the Edit menu does: on the history the keys were last in, or
// last recorded into - the Watchlist's, or the file in the Viewer - and they
// never take the keys, so Ctrl+Z after a click still acts where they were.
// Each tooltip names its step: "Undo: Remove 3 values from the Watchlist
// (Ctrl+Z)", "Undo (Ctrl+Z)" when there is none; a step that waits keeps its
// name, greyed, with why ("Stop polling to change the Watchlist.").
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: buttons
    property UndoRouter router: null
    spacing: 2

    IconToolButton {
        objectName: "undoButton"
        iconName: "undo"
        focusPolicy: Qt.NoFocus
        enabled: !!buttons.router && buttons.router.canUndo
        text: buttons.router && buttons.router.undoText.length > 0 ? qsTr("Undo: %1").arg(buttons.router.undoText)
                                                                   : qsTr("Undo")
        shortcutText: qsTr("Ctrl+Z")
        // Nothing to undo needs no reason; a step that waits says why.
        disabledReason: buttons.router && buttons.router.undoText.length > 0 ? buttons.router.undoReason : ""
        onClicked: buttons.router.undo()
    }
    IconToolButton {
        objectName: "redoButton"
        iconName: "redo"
        focusPolicy: Qt.NoFocus
        enabled: !!buttons.router && buttons.router.canRedo
        text: buttons.router && buttons.router.redoText.length > 0 ? qsTr("Redo: %1").arg(buttons.router.redoText)
                                                                   : qsTr("Redo")
        shortcutText: qsTr("Ctrl+Y")
        disabledReason: buttons.router && buttons.router.redoText.length > 0 ? buttons.router.redoReason : ""
        onClicked: buttons.router.redo()
    }
}
