// The Watchlist's "+" row (WL-4b), as Live Expressions' "Add new expression":
// a path typed or pasted, completed from the device's descriptor, added on
// Enter. At rest it is "+ Add a path"; a click, Insert or Down from the last
// row edits it. Completion shows what may come next; the line under it says
// in words what Enter would add. Several pasted lines are added at once,
// "path = value" lines copied from the Watchlist included. Presentation
// only: the provider resolves and adds. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: addRow
    objectName: "watchAddRow"
    required property var provider
    // Says what an addition did, with Undo, on the panel's notice line.
    signal added(string notice, bool undoable)
    // Up from the field: back to the rows.
    signal leaveUp()
    // Enter on a value already watched: show its row (a source row).
    signal reveal(int row, string notice)
    property bool editing: false
    activeFocusOnTab: !editing
    onActiveFocusChanged: if (activeFocus && !editing) begin()
    property var check: ({ status: "empty", message: "" })
    function recheck() {
        check = editing && field.text.trim().length > 0 ? provider.checkWatchPath(field.text)
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
    function refresh() {
        candidates = editing ? provider.watchPathCandidates(field.text) : []
        highlighted = -1
        const typed = field.text.trim()
        if (editing && candidates.length > 0 && field.activeFocus
                && !(candidates.length === 1 && candidates[0].path === typed)) {
            popup.place()
            popup.open()
        }
        else
            popup.close()
    }
    // A candidate chosen: a container gets "." or "[" and the next step's
    // candidates; a value fills the field, waiting for Enter.
    function choose(index) {
        const c = candidates[index]
        if (!c)
            return
        let text = c.path
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
        if (candidates.length === 1 && candidates[0].container && candidates[0].path.indexOf(" … ") < 0)
            return candidates[0].path + (candidates[0].next || ".")
        const typed = field.text.trim()
        const starting = candidates.filter(c => c.path.startsWith(typed) && c.path.indexOf(" … ") < 0)
        if (starting.length === 0)
            return field.text
        let prefix = starting[0].path
        for (const c of starting)
            while (!c.path.startsWith(prefix))
                prefix = prefix.substring(0, prefix.length - 1)
        return prefix.length > field.text.length ? prefix : field.text
    }
    // Adds what the field names, or every pasted line.
    function add(lines) {
        const result = provider.addWatchPaths(lines)
        if (!result)
            return
        const added = result.added || 0
        const kept = result.missing ? result.missing.length : 0
        const parts = []
        const first = lines[0].split(" = ")[0].split("\t")[0].trim()
        if (lines.length === 1 && kept === 1)
            parts.push(qsTr("Added %1, not on this device").arg(first))
        else if (added > 0)
            parts.push(lines.length === 1 && added === 1 ? qsTr("Added %1").arg(first)
                       : lines.length === 1 ? qsTr("Added %L1 values of %2").arg(added).arg(first)
                       : qsTr("Added %L1").arg(added))
        if (result.already > 0)
            parts.push(qsTr("%L1 already watched").arg(result.already))
        if (kept > 0 && lines.length > 1)
            parts.push(qsTr("%L1 not on this device").arg(kept))
        if (result.refused && result.refused.length > 0)
            parts.push(result.refused.length === 1 ? result.refused[0].reason
                                                   : qsTr("%L1 refused").arg(result.refused.length))
        if (result.status === "capacity" && result.message)
            parts.push(result.message)
        if (result.status !== "ok" && result.status !== "capacity")
            parts.push(result.message || qsTr("Not added: %1").arg(result.status))
        if (parts.length > 0)
            addRow.added(parts.join(" · "), added > 0 || kept > 0)
        if (added > 0 || kept > 0 || (result.already > 0 && parts.length === 1)) {
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
                objectName: "watchAddRest"
                visible: !addRow.editing
                anchors.left: plus.right
                anchors.leftMargin: Type.tight
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Add a path")
                color: Theme.textMuted
                font.pointSize: Type.body
            }
            TextField {
                id: field
                objectName: "watchAddField"
                visible: addRow.editing
                anchors.left: plus.right
                anchors.leftMargin: Type.tight
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: Type.rowHeight
                topPadding: 0
                bottomPadding: 0
                font.family: Type.fixedFamily
                placeholderText: qsTr("device.Config.Gain, data.Samples[3]")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                Accessible.name: qsTr("Add a path to the Watchlist")
                Accessible.description: qsTr("Type or paste paths, one per line. Down opens suggestions; Enter adds.")
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
                        if (!popup.visible && addRow.candidates.length > 0)
                            popup.open()
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
                            addRow.candidates = provider.watchPathCandidates(text)
                            addRow.highlighted = -1
                        }
                        addRow.recheck()
                        if (popup.visible && addRow.highlighted >= 0) {
                            addRow.choose(addRow.highlighted)
                        } else if (addRow.check.status === "watched") {
                            addRow.reveal(addRow.check.row, addRow.check.message)
                            text = ""
                            addRow.refresh()
                        } else if (addRow.check.status === "missing" && checkLine.typing) {
                            // A path the suggestions go on from is half typed, not
                            // one a device lacks: Enter takes the first of them.
                            addRow.choose(0)
                        } else if (addRow.check.status === "adds" || addRow.check.status === "missing") {
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
                objectName: "watchAddPopup"
                // Under the field, above it when there is no room below.
                // Under the field and its line, or above when the window has no room.
                function place() {
                    const below = addRow.Window.height - field.mapToItem(null, 0, field.height).y
                    const under = field.height + (checkLine.visible ? checkLine.height : 0)
                    y = below >= under + height + Type.tight ? under : -height
                }
                margins: 0
                x: field.x
                width: Math.max(Type.bodyPixels * 18, field.width)
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
                    objectName: "watchAddCandidates"
                    clip: true
                    model: addRow.candidates
                    currentIndex: addRow.highlighted
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Item {
                        id: candidate
                        objectName: "watchAddCandidate"
                        required property var modelData
                        required property int index
                        width: candidateList.width
                        height: Type.rowHeight
                        readonly property bool group: modelData.path.endsWith(".")
                        Rectangle {
                            anchors.fill: parent
                            color: candidate.index === addRow.highlighted ? Theme.stateSelected
                                 : pick.hovered ? Theme.stateHover : "transparent"
                        }
                        HoverHandler { id: pick }
                        TapHandler {
                            onTapped: addRow.choose(candidate.index)
                            onDoubleTapped: if (!candidate.modelData.container) addRow.add([candidate.modelData.path])
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Type.padding
                            anchors.rightMargin: Type.padding
                            spacing: Type.spacing
                            // What is typed in full, then the rest in a quieter tone.
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                elide: Text.ElideMiddle
                                font.family: Type.fixedFamily
                                font.weight: candidate.group ? Type.headingWeight : Font.Normal
                                color: candidate.index === addRow.highlighted ? Theme.selectedForeground : Theme.textSecondary
                                text: candidate.modelData.path
                            }
                            Text {
                                visible: candidate.modelData.watched === true
                                text: qsTr("watched")
                                font.pointSize: Type.caption
                                color: candidate.index === addRow.highlighted ? Theme.selectedForeground : Theme.textMuted
                            }
                            Text {
                                text: !candidate.group ? candidate.modelData.typeText
                                    : candidate.modelData.values === 1 ? qsTr("1 Field")
                                    : qsTr("%L1 Fields").arg(candidate.modelData.values)
                                font.family: candidate.group ? Qt.application.font.family : Type.fixedFamily
                                font.pointSize: Type.caption
                                color: candidate.index === addRow.highlighted ? Theme.selectedForeground : Theme.textMuted
                            }
                        }
                        Accessible.role: Accessible.ListItem
                        Accessible.name: [candidate.modelData.path, candidate.modelData.typeText,
                                          candidate.modelData.watched === true ? qsTr("watched") : ""].filter(part => (part || "").length > 0).join(", ")
                    }
                }
            }
        }
        // What Enter would add, or why not, in words.
        Label {
            id: checkLine
            objectName: "watchAddCheck"
            readonly property bool typing: addRow.candidates.some(c => c.path.startsWith(field.text.trim())
                                                                  && c.path !== field.text.trim())
            visible: addRow.editing && text.length > 0 && !(typing && addRow.check.status !== "adds"
                                                              && addRow.check.status !== "watched")
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding + Type.indent + Type.tight
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: addRow.check.message || ""
            font.pointSize: Type.caption
            color: addRow.check.status === "adds" || addRow.check.status === "watched" || addRow.check.status === "missing"
                   ? Theme.textSecondary : Theme.error
        }
    }
}
