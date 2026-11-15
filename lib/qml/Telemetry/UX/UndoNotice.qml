// A panel's line for what its last action did, with that step's Undo (UR-1):
// "Removed device.Config.Gain · Undo". MIT.
//
// `history` is the UndoHistory the panel's provider records into. say(message,
// serial, detail) shows a message and the step it speaks of (0: none). Its
// link offers Undo while that step is the one Undo would take, and Redo once
// it is undone. Taken - by the link, Ctrl+Z or the Edit menu - the line says
// how: "Undone: Remove device.Config.Gain from the Watchlist · Redo", what
// could not be put back after it, and a skipped or partial step in the
// warning colour. The line also speaks for an undo or redo elsewhere of a
// step of its `kinds` ("watch.rows"): the panel that owns them says it, and
// tells the history so (markNoticed) - the window's status line need not.
// recording(action) answers the serial of the step an action recorded, for
// an action that answers none itself.
//
// `revision` is what the line speaks of - a provider's watchRevision: when it
// moves the line goes, as what it said no longer holds. `standIn` is shown
// instead while it is set, with no link ("Stop polling to change the
// Watchlist."). `detail` is the whole sentence when the line is short: the
// tooltip of a cut line and what a screen reader hears. The link's name
// carries its step ("Undo: Remove device.Config.Gain from the Watchlist").
// What the link takes is read aloud here; what a key takes, by the router.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: notice
    property var history: null
    property var kinds: []
    property string message: ""
    property string detail: ""
    property int serial: 0
    property int revision: -1
    property string standIn: ""
    property string standInDetail: ""
    // The host's own condition for the line to be seen (a folded list's is false).
    property bool active: true
    property color color: Theme.textSecondary
    property real pointSize: Type.caption
    // What the line shows, and its whole sentence.
    readonly property string text: standIn.length > 0 ? standIn : message
    readonly property string fullText: standIn.length > 0 ? (standInDetail.length > 0 ? standInDetail : standIn)
                                                          : detail.length > 0 ? detail : message
    // Something could not be put back: said in the warning colour.
    property bool exception: false
    readonly property bool offersUndo: standIn.length === 0 && serial > 0 && history !== null
                                       && history.canUndo && history.undoSerial === serial
    readonly property bool offersRedo: !offersUndo && standIn.length === 0 && serial > 0 && history !== null
                                       && history.canRedo && history.redoSerial === serial
    // The revision the line spoke at; the step it last spoke of, kept when the
    // line goes - an undo changes the revision before it says what it took;
    // the link's own undo or redo under way.
    property int spokeAt: -1
    property int spokeOf: 0
    property bool taking: false
    visible: active && text.length > 0
    spacing: Type.spacing

    function say(message, serial, detail) {
        notice.message = message
        notice.detail = typeof detail === "string" ? detail : ""
        notice.serial = typeof serial === "number" ? serial : 0
        notice.exception = false
        spokeAt = revision
        spokeOf = notice.serial
        const heard = notice.detail.length > 0 ? notice.detail : message
        if (heard.length > 0)
            notice.Accessible.announce(heard)
    }
    function clear() {
        message = ""
        detail = ""
        serial = 0
        exception = false
    }
    // Runs action and answers the serial of the step it recorded - the one it
    // merged into, when it did - or 0 when it recorded none.
    function recording(action) {
        const recorder = history
        let held = 0
        const heard = serial => { held = serial }
        if (recorder)
            recorder.recorded.connect(heard)
        try {
            action()
        } finally {
            if (recorder)
                recorder.recorded.disconnect(heard)
        }
        return held
    }
    // Takes the step the link offers.
    function take() {
        if (!history || !(offersUndo || offersRedo))
            return false
        taking = true
        const taken = offersUndo ? history.undo() : history.redo()
        taking = false
        return taken
    }
    onRevisionChanged: {
        if (message.length > 0 && revision !== spokeAt)
            clear()
    }
    Connections {
        target: notice.history
        function onDone(serial, text, undone, note, outcome, kinds) {
            const owned = (serial > 0 && serial === notice.spokeOf)
                          || (kinds || []).some(kind => notice.kinds.indexOf(kind) >= 0)
            // Spoken for only where it is seen: a hidden panel leaves it to the status line.
            if (!owned || !notice.active || !notice.parent || !notice.parent.visible)
                return
            const words = notice.history.sentence(text, undone, note, outcome)
            notice.message = words
            notice.detail = ""
            notice.serial = outcome === "skipped" ? 0 : serial
            notice.exception = outcome !== "done"
            notice.spokeAt = notice.revision
            notice.spokeOf = notice.serial
            notice.history.markNoticed()
            if (notice.taking)
                notice.Accessible.announce(words)
        }
    }

    Label {
        id: line
        objectName: "undoNoticeText"
        // As wide as its words, cut only when the line is too narrow for them
        // and the link: the only part of the line that gives way.
        Layout.minimumWidth: 0
        Layout.alignment: Qt.AlignVCenter
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.PlainText
        text: notice.text
        color: notice.exception ? Theme.warning : notice.color
        font.pointSize: notice.pointSize
        elide: Text.ElideRight
        Accessible.description: notice.fullText
        HoverHandler { id: lineHover }
        PlainToolTip {
            visible: lineHover.hovered && notice.fullText.length > 0
                     && (line.truncated || notice.fullText !== line.text)
            text: notice.fullText
        }
    }
    Label {
        id: dot
        visible: link.visible
        Layout.minimumWidth: implicitWidth
        text: "·"
        color: Theme.textMuted
        font.pointSize: notice.pointSize
        Accessible.ignored: true
    }
    LinkButton {
        id: link
        objectName: "undoNoticeLink"
        visible: notice.offersUndo || notice.offersRedo
        Layout.minimumWidth: implicitWidth
        text: notice.offersUndo ? qsTr("Undo") : qsTr("Redo")
        Accessible.name: notice.offersUndo ? qsTr("Undo: %1").arg(notice.history.undoText)
                       : notice.offersRedo ? qsTr("Redo: %1").arg(notice.history.redoText) : text
        font.pointSize: notice.pointSize
        onClicked: notice.take()
    }
    Item { Layout.fillWidth: true }
}
