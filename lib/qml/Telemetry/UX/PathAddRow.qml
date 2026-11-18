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
// values?, section?}], with headings among them where an adapter groups its
// candidates - {heading: true, path (the heading's words), completion (what
// a click writes), count, shown} - which the keys skip (the Batch's Fields,
// Commands and Services); add(lines) -> {notice, undoable, serial, clear} (serial: the
// step the addition recorded, 0 when none); enter(text) -> true
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
    // The suggestions open by themselves on an empty field; without it, Down
    // opens them (the Batch's first of each kind).
    property bool openOnEmpty: true
    // Enter adds at once (the Batch): the highlighted suggestion, else the
    // best match, else what the line names - never only filling the field.
    // Without it, Enter on a suggestion fills the field for a second Enter.
    property bool enterAdds: false
    // The best match is highlighted as the suggestions come (the Batch), so
    // the row shows what Enter takes.
    property bool highlightBest: false
    // The highlight was moved by the keys: Enter takes it over the line.
    property bool pickedByKeys: false
    // Where the "+" stands and how wide its column is, and where the words
    // and the field start: by default the "+" in one indent step, the words
    // after it. A list with columns (the Batch's) puts the "+" in its
    // number column and the words under its first text column.
    property real plusX: Type.padding
    property real plusWidth: Type.indent
    property int plusAlignment: Text.AlignHCenter
    property real textX: plusX + plusWidth + Type.tight
    // Says what an addition did, on the panel's notice line, and the step it
    // recorded (UR-1), 0 when none.
    signal added(string notice, bool undoable, int serial)
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
    // A heading among the candidates: never highlighted, never added.
    function isHeading(c) {
        return c !== undefined && c !== null && c.heading === true
    }
    // The first candidate that is not a heading, from index on in step (+1
    // or -1); -1 when there is none.
    function pickFrom(index, step) {
        for (let at = index; at >= 0 && at < candidates.length; at += step)
            if (!isHeading(candidates[at]))
                return at
        return -1
    }
    function firstPick() {
        return pickFrom(0, 1)
    }
    // "Commands, 2 found", or "Fields, 8 of 120 shown".
    function headingName(c) {
        return c.shown !== undefined && c.shown < c.count ? qsTr("%1, %L2 of %L3 shown").arg(c.path).arg(c.shown).arg(c.count)
                                                          : qsTr("%1, %L2 found").arg(c.path).arg(c.count || 0)
    }
    function candidateName(index) {
        const c = candidates[index]
        if (!c)
            return ""
        if (isHeading(c))
            return headingName(c)
        return [completionOf(c), c.kind || "", c.typeText || "", c.note || ""].filter(part => part.length > 0).join(", ")
    }
    function refresh() {
        candidates = editing ? adapter.candidates(field.text) : []
        highlighted = -1
        pickedByKeys = false
        const typed = field.text.trim()
        if (highlightBest && editing && typed.length > 0)
            highlighted = firstPick()
        const picks = candidates.filter(c => !isHeading(c))
        if (editing && picks.length > 0 && field.activeFocus && (openOnEmpty || typed.length > 0)
                && !(picks.length === 1 && completionOf(picks[0]) === typed)) {
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
        if (firstPick() < 0)
            return false
        popup.place()
        popup.open()
        highlighted = firstPick()
        return true
    }
    // A container's path with the step into it: "." or "[" after it, unless
    // the path already ends there, as a top-level group's "device." does.
    function into(c) {
        const path = completionOf(c)
        return path.endsWith(".") || path.endsWith("[") ? path : path + (c.next || ".")
    }
    // A candidate chosen: a container gets "." or "[" and the next step's
    // candidates; a heading writes its kind word before the text and lists
    // that kind; a value fills the field, waiting for Enter.
    function choose(index) {
        const c = candidates[index]
        if (!c)
            return
        let text = completionOf(c)
        const range = text.indexOf(" … ")
        if (range >= 0)
            text = text.substring(0, text.lastIndexOf("[") + 1)
        else if (c.container)
            text = into(c)
        field.text = text
        field.cursorPosition = text.length
        refresh()
        recheck()
        if (!c.container && !isHeading(c))
            popup.close()
    }
    // The candidates' common prefix, for Tab.
    function common() {
        if (candidates.length === 0)
            return field.text
        const picks = candidates.filter(c => !isHeading(c))
        if (picks.length === 0)
            return field.text
        // One container left: into it.
        if (picks.length === 1 && picks[0].container && completionOf(picks[0]).indexOf(" … ") < 0)
            return into(picks[0])
        const typed = field.text.trim()
        const starting = picks.map(c => completionOf(c))
                                   .filter(path => path.startsWith(typed) && path.indexOf(" … ") < 0)
        if (starting.length === 0)
            return field.text
        let prefix = starting[0]
        for (const path of starting)
            while (!path.startsWith(prefix))
                prefix = prefix.substring(0, prefix.length - 1)
        return prefix.length > field.text.length ? prefix : field.text
    }
    // A suggestion taken by Enter: a heading narrows to its kind and a
    // container is stepped into, as a click does; anything else is added.
    function take(index) {
        const c = candidates[index]
        if (!c)
            return false
        if (isHeading(c) || c.container) {
            choose(index)
            return true
        }
        popup.close()
        add([completionOf(c)])
        return true
    }
    // Enter where it adds at once (enterAdds): a suggestion the keys moved
    // to; else the endpoint the line names; else, for a half-typed name or
    // one two endpoints share, the highlighted or first suggestion. A line
    // that names nothing says why and adds nothing.
    function enterAdd() {
        const picked = popup.visible && highlighted >= 0 ? highlighted : -1
        if (pickedByKeys && picked >= 0)
            return take(picked)
        const status = check.status
        if (addStatuses.indexOf(status) >= 0) {
            popup.close()
            add([field.text])
            return true
        }
        // Two endpoints share the name and the list is shut: show them first.
        if (status === "choose" && !popup.visible)
            return offer()
        if (status === "partial" || status === "choose" || (status === "missing" && candidates.length > 0)) {
            const at = highlighted >= 0 ? highlighted : firstPick()
            return at >= 0 ? take(at) : false
        }
        return false
    }
    // The line as an adapter writes it; the suggestions follow it.
    function setText(value) {
        field.text = value
        field.cursorPosition = value.length
        refresh()
        recheck()
    }
    // Adds what the field names, or every pasted line.
    function add(lines) {
        const result = adapter.add(lines)
        if (!result)
            return
        if (result.notice && result.notice.length > 0)
            addRow.added(result.notice, result.undoable === true,
                         typeof result.serial === "number" ? result.serial : 0)
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
                objectName: addRow.namePrefix + "Plus"
                x: addRow.plusX
                width: addRow.plusWidth
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: addRow.plusAlignment
                text: "+"
                color: Theme.textSecondary
                font.pointSize: Type.body
                Accessible.ignored: true
            }
            Text {
                objectName: addRow.namePrefix + "Rest"
                visible: !addRow.editing
                x: addRow.textX
                anchors.verticalCenter: parent.verticalCenter
                text: addRow.restText
                color: Theme.textMuted
                font.pointSize: Type.body
            }
            TextField {
                id: field
                objectName: addRow.namePrefix + "Field"
                visible: addRow.editing
                x: addRow.textX
                width: Math.max(0, parent.width - x)
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
                        // An empty field lists what there is first (the Batch's
                        // first of each kind).
                        if (addRow.candidates.length === 0 && text.trim().length === 0)
                            addRow.candidates = addRow.adapter.candidates(text)
                        if (!popup.visible && addRow.candidates.length > 0) {
                            popup.place()
                            popup.open()
                        }
                        // The headings are skipped.
                        const next = addRow.pickFrom(addRow.highlighted + 1, 1)
                        if (next >= 0) {
                            addRow.highlighted = next
                            addRow.pickedByKeys = true
                        }
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up) {
                        const previous = addRow.highlighted > 0 ? addRow.pickFrom(addRow.highlighted - 1, -1) : -1
                        if (previous >= 0) {
                            addRow.highlighted = previous
                            addRow.pickedByKeys = true
                        } else if (addRow.highlighted >= 0) {
                            addRow.highlighted = -1
                            addRow.pickedByKeys = false
                        }
                        else {
                            popup.close()
                            addRow.leaveUp()
                        }
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backtab) {
                        // Leaving backwards: the suggestions go with the field.
                        popup.close()
                        event.accepted = false
                    } else if (event.key === Qt.Key_Tab && (popup.visible || addRow.common() !== text)) {
                        text = addRow.common()
                        cursorPosition = text.length
                        addRow.refresh()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        // Typed since the suggestions were made: make them again,
                        // so Enter never decides on the text before.
                        if (settleTimer.running) {
                            settleTimer.stop()
                            if (addRow.enterAdds) {
                                addRow.refresh()
                            } else {
                                addRow.candidates = addRow.adapter.candidates(text)
                                addRow.highlighted = -1
                            }
                        }
                        addRow.recheck()
                        if (addRow.enterAdds) {
                            addRow.enterAdd()
                        } else if (popup.visible && addRow.highlighted >= 0) {
                            addRow.choose(addRow.highlighted)
                        } else if (addRow.adapter.enter(text)) {
                            // the adapter did what Enter means for it
                        } else if (addRow.check.status === "missing" && checkLine.typing) {
                            // A path the suggestions go on from is half typed, not
                            // one a device lacks: Enter takes the first of them.
                            addRow.choose(addRow.firstPick())
                        } else if (addRow.addStatuses.indexOf(addRow.check.status) >= 0) {
                            addRow.add([text])
                        }
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        // Where a click away adds (enterAdds), Escape cancels:
                        // the field empties as it was before.
                        if (popup.visible) {
                            popup.close()
                        } else {
                            if (addRow.enterAdds)
                                addRow.clear()
                            addRow.finish()
                        }
                        event.accepted = true
                    }
                }
                onActiveFocusChanged: {
                    // Focus gone elsewhere with a line that names one endpoint:
                    // added as Enter adds it (enterAdds). Anything else stays
                    // in the field, its line saying why.
                    if (!activeFocus && addRow.enterAdds && addRow.editing && text.trim().length > 0) {
                        if (settleTimer.running) {
                            settleTimer.stop()
                            addRow.refresh()
                        }
                        addRow.recheck()
                        if (addRow.addStatuses.indexOf(addRow.check.status) >= 0) {
                            popup.close()
                            addRow.add([text])
                        }
                    }
                    if (!activeFocus && !popup.visible && text.length === 0)
                        addRow.editing = false
                }
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
                        readonly property bool heading: addRow.isHeading(modelData)
                        readonly property bool group: !heading && (modelData.path.endsWith(".") || modelData.kindCode === "group")
                        readonly property bool chosen: index === addRow.highlighted
                        // A word after the path: "watched", "in Batch".
                        readonly property string note: typeof modelData.note === "string" && modelData.note.length > 0
                                                       ? modelData.note : modelData.watched === true ? qsTr("watched") : ""
                        readonly property string detail: candidate.group
                            ? (modelData.values === undefined ? qsTr("group")
                               : modelData.values === 1 ? qsTr("1 Field") : qsTr("%L1 Fields").arg(modelData.values))
                            // Under a heading the kind goes without saying.
                            : [modelData.section !== undefined ? "" : modelData.kind || "", modelData.typeText || ""]
                                  .filter(part => part.length > 0).join("  ")
                        Rectangle {
                            anchors.fill: parent
                            color: candidate.chosen ? Theme.stateSelected : pick.hovered ? Theme.stateHover : "transparent"
                        }
                        HoverHandler { id: pick }
                        TapHandler {
                            onTapped: addRow.choose(candidate.index)
                            onDoubleTapped: if (!candidate.modelData.container && !candidate.heading)
                                addRow.add([addRow.completionOf(candidate.modelData)])
                        }
                        // A heading: "Commands  2", a click shows that kind only.
                        RowLayout {
                            visible: candidate.heading
                            anchors.fill: parent
                            anchors.leftMargin: Type.padding
                            anchors.rightMargin: Type.padding
                            spacing: Type.spacing
                            Text {
                                textFormat: Text.PlainText
                                text: candidate.modelData.path
                                font.pointSize: Type.caption
                                font.weight: Type.headingWeight
                                color: Theme.textSecondary
                            }
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: candidate.modelData.shown !== undefined && candidate.modelData.shown < candidate.modelData.count
                                      ? qsTr("%L1 of %L2").arg(candidate.modelData.shown).arg(candidate.modelData.count)
                                      : qsTr("%L1").arg(candidate.modelData.count || 0)
                                font.pointSize: Type.caption
                                color: Theme.textMuted
                            }
                            PlainToolTip {
                                visible: candidate.heading && pick.hovered
                                delay: 700
                                text: qsTr("Show only %1 - or type \"%2 \" first").arg(candidate.modelData.path).arg(candidate.modelData.word || "")
                            }
                        }
                        RowLayout {
                            visible: !candidate.heading
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
                        Accessible.role: candidate.heading ? Accessible.Heading : Accessible.ListItem
                        Accessible.name: candidate.heading ? addRow.headingName(candidate.modelData)
                            : [candidate.modelData.path, candidate.detail, candidate.note]
                                .filter(part => (part || "").length > 0).join(", ")
                    }
                }
            }
        }
        // What Enter would do, or why not, in words.
        Label {
            id: checkLine
            objectName: addRow.namePrefix + "Check"
            readonly property bool typing: addRow.candidates.some(c => !addRow.isHeading(c)
                                                                  && addRow.completionOf(c).startsWith(field.text.trim())
                                                                  && addRow.completionOf(c) !== field.text.trim())
            visible: addRow.editing && text.length > 0
                     && !(addRow.hideWhileTyping && typing && addRow.typingStatuses.indexOf(addRow.check.status) < 0)
            // The suggestions sit under this line: they follow it.
            onVisibleChanged: if (popup.visible) popup.place()
            onHeightChanged: if (popup.visible) popup.place()
            Layout.fillWidth: true
            Layout.leftMargin: addRow.textX
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: addRow.check.message || ""
            font.pointSize: Type.caption
            color: addRow.quietStatuses.indexOf(addRow.check.status) >= 0 ? Theme.textSecondary : Theme.error
        }
    }
}
