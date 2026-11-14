// A "+" row that takes a typed or pasted path, completes it from the device's
// descriptor and adds what it names on Enter: the Watchlist's (WatchAddRow)
// and the Batch's (BatchAddRow) are this row with an adapter of their own.
// At rest it is "+ <restText>"; a click, Insert or Down from the last row
// edits it. The line under it says in words what Enter would do. Several
// pasted lines are added at once. Presentation only: the adapter's provider
// resolves and adds. MIT.
//
// The adapter: check(text) -> {status, message, ...}; candidates(text) ->
// [{path, completion?, kind?, typeText, note?, watched?, container, next?,
// values?}]; add(lines) -> {notice, undoable, clear}; enter(text) -> true
// when it handled Enter itself (before the row adds).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: addRow
    required property var adapter
    // The children's objectNames: prefix + "Rest", "Field", "Popup",
    // "Candidates", "Candidate", "Check".
    property string namePrefix: "pathAdd"
    property string restText: qsTr("Add a path")
    property string placeholderText: ""
    property string accessibleName: ""
    property string accessibleDescription: ""
    // Statuses said in the quiet colour; the others are refusals.
    property var quietStatuses: ["adds"]
    // Statuses on which Enter adds the typed line.
    property var addStatuses: ["adds"]
    // Statuses still said while the suggestions go on from the text; with
    // hideWhileTyping false every status is said at once.
    property var typingStatuses: ["adds"]
    property bool hideWhileTyping: true
    // The suggestions' widest; a full-width field need not make them so.
    property real popupMaxWidth: Number.POSITIVE_INFINITY
    // Says what an addition did, on the panel's notice line.
    signal added(string notice, bool undoable)
    // Up from the field: back to the rows.
    signal leaveUp()
    property bool editing: false
    activeFocusOnTab: !editing
    onActiveFocusChanged: if (activeFocus && !editing) begin()
    property var check: ({ status: "empty", message: "" })
    readonly property alias text: field.text
    readonly property bool typing: checkLine.typing
    function recheck() {
        check = editing && field.text.trim().length > 0 ? adapter.check(field.text)
                                                        : ({ status: "empty", message: "" })
    }
    // A moment after the last key: the words and the candidates, not on every key.
    Timer { id: settleTimer; interval: 80; onTriggered: { addRow.refresh(); addRow.recheck() } }
    property var candidates: []
    property int highlighted: -1
    implicitHeight: column.implicitHeight
    implicitWidth: column.implicitWidth

    function begin() {
        editing = true
        field.forceActiveFocus()
        refresh()
    }
    function finish() {
        popup.close()
        editing = field.text.length > 0
        addRow.leaveUp()
    }
    function clear() {
        field.text = ""
        refresh()
        recheck()
    }
    // Turned off (a Batch runs): the suggestions close; turned on again, the
    // field takes the keys back if it had them.
    property bool resumeEditing: false
    onEnabledChanged: {
        if (!enabled) {
            resumeEditing = editing && field.activeFocus
            popup.close()
        } else if (resumeEditing) {
            resumeEditing = false
            field.forceActiveFocus()
        }
    }
    function candidateName(index) {
        const c = candidates[index]
        if (!c)
            return ""
        return [completionOf(c), c.kind || "", c.typeText || "", c.note || ""].filter(part => part.length > 0).join(", ")
    }
    function refresh() {
        candidates = editing ? adapter.candidates(field.text) : []
        highlighted = -1
        const typed = field.text.trim()
        if (editing && candidates.length > 0 && field.activeFocus
                && !(candidates.length === 1 && completionOf(candidates[0]) === typed)) {
            popup.place()
            popup.open()
        }
        else
            popup.close()
    }
    // What a candidate puts in the field.
    function completionOf(c) {
        return c.completion || c.path
    }
    // The suggestions opened on the first of them, for Enter to take.
    function offer() {
        if (candidates.length === 0)
            return false
        popup.place()
        popup.open()
        highlighted = 0
        return true
    }
    // A candidate chosen: a container gets "." or "[" and the next step's
    // candidates; a value fills the field, waiting for Enter.
    function choose(index) {
        const c = candidates[index]
        if (!c)
            return
        let text = completionOf(c)
        const range = text.indexOf(" … ")
        if (range >= 0)
            text = text.substring(0, text.lastIndexOf("[") + 1)
        else if (c.container)
            text += c.next || "."
        field.text = text
        field.cursorPosition = text.length
        refresh()
        recheck()
        if (!c.container)
            popup.close()
    }
    // The candidates' common prefix, for Tab.
    function common() {
        if (candidates.length === 0)
            return field.text
        // One container left: into it.
        if (candidates.length === 1 && candidates[0].container && completionOf(candidates[0]).indexOf(" … ") < 0)
            return completionOf(candidates[0]) + (candidates[0].next || ".")
        const typed = field.text.trim()
        const starting = candidates.map(c => completionOf(c))
                                   .filter(path => path.startsWith(typed) && path.indexOf(" … ") < 0)
        if (starting.length === 0)
            return field.text
        let prefix = starting[0]
        for (const path of starting)
            while (!path.startsWith(prefix))
                prefix = prefix.substring(0, prefix.length - 1)
        return prefix.length > field.text.length ? prefix : field.text
    }
    // Adds what the field names, or every pasted line.
    function add(lines) {
        const result = adapter.add(lines)
        if (!result)
            return
        if (result.notice && result.notice.length > 0)
            addRow.added(result.notice, result.undoable === true)
        if (result.clear === true) {
            field.text = ""
            refresh()
        }
        recheck()
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Type.rowHeight
            Rectangle {
                anchors.fill: parent
                color: !addRow.editing && restHover.hovered ? Theme.stateHover : "transparent"
            }
            HoverHandler { id: restHover; enabled: !addRow.editing }
            TapHandler { enabled: !addRow.editing; onTapped: addRow.begin() }
            Text {
                id: plus
                x: Type.padding
                width: Type.indent
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                text: "+"
                color: Theme.textSecondary
                font.pointSize: Type.body
                Accessible.ignored: true
            }
            Text {
                objectName: addRow.namePrefix + "Rest"
                visible: !addRow.editing
                anchors.left: plus.right
                anchors.leftMargin: Type.tight
                anchors.verticalCenter: parent.verticalCenter
                text: addRow.restText
                color: Theme.textMuted
                font.pointSize: Type.body
            }
            TextField {
                id: field
                objectName: addRow.namePrefix + "Field"
                visible: addRow.editing
                anchors.left: plus.right
                anchors.leftMargin: Type.tight
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: Type.rowHeight
                topPadding: 0
                bottomPadding: 0
                font.family: Type.fixedFamily
                placeholderText: addRow.placeholderText
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                Accessible.name: addRow.accessibleName
                // The highlighted suggestion while arrowing through them,
                // else what Enter would do.
                Accessible.description: addRow.highlighted >= 0 ? addRow.candidateName(addRow.highlighted)
                                        : addRow.check.message && addRow.check.message.length > 0
                                        ? addRow.check.message : addRow.accessibleDescription
                onTextEdited: {
                    // Several lines pasted: added at once, not into the field.
                    if (text.indexOf("\n") >= 0 || text.indexOf("\r") >= 0) {
                        const lines = text.split(/\r?\n|\r/).map(line => line.trim()).filter(line => line.length > 0)
                        text = ""
                        if (lines.length > 0)
                            addRow.add(lines)
                        return
                    }
                    settleTimer.restart()
                }
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Down) {
                        if (!popup.visible && addRow.candidates.length > 0) {
                            popup.place()
                            popup.open()
                        }
                        addRow.highlighted = Math.min(addRow.highlighted + 1, addRow.candidates.length - 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up) {
                        if (addRow.highlighted > 0)
                            addRow.highlighted -= 1
                        else if (addRow.highlighted === 0)
                            addRow.highlighted = -1
                        else {
                            popup.close()
                            addRow.leaveUp()
                        }
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab) {
                        // Leaving backwards: the suggestions go with the field.
                        popup.close()
                        event.accepted = false
                    } else if (event.key === Qt.Key_Tab && popup.visible) {
                        text = addRow.common()
                        cursorPosition = text.length
                        addRow.refresh()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        // Typed since the suggestions were made: make them again,
                        // so Enter never decides on the text before.
                        if (settleTimer.running) {
                            settleTimer.stop()
                            addRow.candidates = addRow.adapter.candidates(text)
                            addRow.highlighted = -1
                        }
                        addRow.recheck()
                        if (popup.visible && addRow.highlighted >= 0) {
                            addRow.choose(addRow.highlighted)
                        } else if (addRow.adapter.enter(text)) {
                            // the adapter did what Enter means for it
                        } else if (addRow.check.status === "missing" && checkLine.typing) {
                            // A path the suggestions go on from is half typed, not
                            // one a device lacks: Enter takes the first of them.
                            addRow.choose(0)
                        } else if (addRow.addStatuses.indexOf(addRow.check.status) >= 0) {
                            addRow.add([text])
                        }
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        if (popup.visible)
                            popup.close()
                        else
                            addRow.finish()
                        event.accepted = true
                    }
                }
                onActiveFocusChanged: if (!activeFocus && !popup.visible && text.length === 0) addRow.editing = false
            }
            Popup {
                id: popup
                objectName: addRow.namePrefix + "Popup"
                // Under the field and its line, or above when the window has no room.
                function place() {
                    const below = addRow.Window.height - field.mapToItem(null, 0, field.height).y
                    const under = field.height + (checkLine.visible ? checkLine.height : 0)
                    y = below >= under + height + Type.tight ? under : -height
                }
                onAboutToShow: place()
                margins: 0
                x: field.x
                width: Math.min(addRow.popupMaxWidth, Math.max(Type.bodyPixels * 18, field.width))
                height: Math.min(addRow.candidates.length, 8) * Type.rowHeight + topPadding + bottomPadding
                padding: 1
                focus: false
                closePolicy: Popup.CloseOnPressOutsideParent
                background: Rectangle {
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.outline
                    radius: Type.radius
                }
                contentItem: ListView {
                    id: candidateList
                    objectName: addRow.namePrefix + "Candidates"
                    clip: true
                    model: addRow.candidates
                    currentIndex: addRow.highlighted
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Item {
                        id: candidate
                        objectName: addRow.namePrefix + "Candidate"
                        required property var modelData
                        required property int index
                        width: candidateList.width
                        height: Type.rowHeight
                        readonly property bool group: modelData.path.endsWith(".") || modelData.kindCode === "group"
                        readonly property bool chosen: index === addRow.highlighted
                        // A word after the path: "watched", "in Batch".
                        readonly property string note: typeof modelData.note === "string" && modelData.note.length > 0
                                                       ? modelData.note : modelData.watched === true ? qsTr("watched") : ""
                        readonly property string detail: candidate.group
                            ? (modelData.values === undefined ? qsTr("group")
                               : modelData.values === 1 ? qsTr("1 Field") : qsTr("%L1 Fields").arg(modelData.values))
                            : [modelData.kind || "", modelData.typeText || ""].filter(part => part.length > 0).join("  ")
                        Rectangle {
                            anchors.fill: parent
                            color: candidate.chosen ? Theme.stateSelected : pick.hovered ? Theme.stateHover : "transparent"
                        }
                        HoverHandler { id: pick }
                        TapHandler {
                            onTapped: addRow.choose(candidate.index)
                            onDoubleTapped: if (!candidate.modelData.container) addRow.add([addRow.completionOf(candidate.modelData)])
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Type.padding
                            anchors.rightMargin: Type.padding
                            spacing: Type.spacing
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                elide: Text.ElideMiddle
                                font.family: Type.fixedFamily
                                font.weight: candidate.group ? Type.headingWeight : Font.Normal
                                color: candidate.chosen ? Theme.selectedForeground : Theme.textSecondary
                                text: candidate.modelData.path
                            }
                            Text {
                                visible: candidate.note.length > 0
                                textFormat: Text.PlainText
                                text: candidate.note
                                font.pointSize: Type.caption
                                color: candidate.chosen ? Theme.selectedForeground : Theme.textMuted
                            }
                            Text {
                                textFormat: Text.PlainText
                                text: candidate.detail
                                font.family: candidate.group ? Qt.application.font.family : Type.fixedFamily
                                font.pointSize: Type.caption
                                color: candidate.chosen ? Theme.selectedForeground : Theme.textMuted
                            }
                        }
                        Accessible.role: Accessible.ListItem
                        Accessible.name: [candidate.modelData.path, candidate.detail, candidate.note]
                            .filter(part => (part || "").length > 0).join(", ")
                    }
                }
            }
        }
        // What Enter would do, or why not, in words.
        Label {
            id: checkLine
            objectName: addRow.namePrefix + "Check"
            readonly property bool typing: addRow.candidates.some(c => addRow.completionOf(c).startsWith(field.text.trim())
                                                                  && addRow.completionOf(c) !== field.text.trim())
            visible: addRow.editing && text.length > 0
                     && !(addRow.hideWhileTyping && typing && addRow.typingStatuses.indexOf(addRow.check.status) < 0)
            // The suggestions sit under this line: they follow it.
            onVisibleChanged: if (popup.visible) popup.place()
            onHeightChanged: if (popup.visible) popup.place()
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding + Type.indent + Type.tight
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: addRow.check.message || ""
            font.pointSize: Type.caption
            color: addRow.quietStatuses.indexOf(addRow.check.status) >= 0 ? Theme.textSecondary : Theme.error
        }
    }
}
