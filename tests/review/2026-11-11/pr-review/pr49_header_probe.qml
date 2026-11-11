// PR #49 review probe: EndpointPanel header actions in every mode, at narrow,
// boundary and wide widths. Run by run_pr49_probe.py with the panel's QML copied
// beside it as "ux". Prints one line per case and exits 0 (findings are data).
import QtQuick
import QtQuick.Window
import "ux"

Window {
    id: window
    visible: true
    width: 360
    height: 640
    ListModel { id: emptyValues }
    ListModel { id: emptyResponses }
    QtObject {
        id: fake
        property string selection: "measure.A_very_long_endpoint_name_for_layout_testing"
        property bool busy: false
        property bool editing: false
        property bool commandSelected: false
        property bool serviceSelected: false
        property bool canEdit: true
        property bool canApply: true
        property bool canExecute: true
        property bool canCallService: true
        property bool canRefresh: true
        property string serviceResponseEndpoint: ""
        property string writeOutcome: ""
        property string commandOutcome: ""
        property string serviceOutcome: ""
        property var values: emptyValues
        property var responses: emptyResponses
        signal readFinished(bool published)
        function beginEdit() {}
        function cancelEdit() {}
        function apply() {}
        function refresh() {}
        function executeCommand() {}
        function callService() {}
    }
    EndpointPanel { id: panel; anchors.fill: parent; provider: fake }

    function find(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = find(item.children[i], name)
            if (found) return found
        }
        return null
    }
    property var modes: [
        {name: "view", busy: false, editing: false, command: false, service: false},
        {name: "edit", busy: false, editing: true, command: false, service: false},
        {name: "edit-busy", busy: true, editing: true, command: false, service: false},
        {name: "command", busy: false, editing: false, command: true, service: false},
        {name: "service", busy: false, editing: false, command: false, service: true}
    ]
    property var titles: ["measure.A_very_long_endpoint_name_for_layout_testing", "a"]
    // Content widths: the header GridLayout switches to two columns at 560.
    property var contentWidths: [256, 296, 336, 559, 560, 561, 600, 700, 1136]
    property int step: 0
    property int failures: 0
    Timer {
        id: tick
        interval: 40
        repeat: false
        property bool measuring: false
        onTriggered: {
            const total = window.modes.length * window.titles.length * window.contentWidths.length
            if (window.step >= total) {
                console.log("PROBE-DONE failures=" + window.failures)
                Qt.exit(0)
                return
            }
            const w = window.step % window.contentWidths.length
            const t = Math.floor(window.step / window.contentWidths.length) % window.titles.length
            const m = Math.floor(window.step / (window.contentWidths.length * window.titles.length))
            const mode = window.modes[m]
            if (!measuring) {
                fake.busy = mode.busy; fake.editing = mode.editing
                fake.commandSelected = mode.command; fake.serviceSelected = mode.service
                fake.selection = window.titles[t]
                window.width = window.contentWidths[w] + panel.leftPadding + panel.rightPadding
                measuring = true
                tick.start()
                return
            }
            measuring = false
            const actions = window.find(panel, "endpointSelectionActions")
            const title = window.find(panel, "endpointSelectionTitle")
            const grid = actions.parent
            const left = panel.leftPadding, right = panel.width - panel.rightPadding
            const shown = []
            for (let i = 0; i < actions.children.length; ++i) {
                const c = actions.children[i]
                if (!c.visible) continue
                const p = c.mapToItem(panel, 0, 0)
                shown.push({name: c.objectName || "busy", x: p.x, y: p.y, r: p.x + c.width})
            }
            let lines = {}
            let outside = []
            for (const s of shown) {
                lines[Math.round(s.y)] = true
                if (s.x < left - 1 || s.r > right + 1) outside.push(s.name)
            }
            const lineCount = Object.keys(lines).length
            const tp = title.mapToItem(panel, 0, 0)
            const titleOutside = tp.x < left - 1 || tp.x + title.width > right + 1
            const wrappedWide = grid.columns === 2 && lineCount > 1
            const bad = outside.length > 0 || titleOutside || wrappedWide
            if (bad) ++window.failures
            console.log((bad ? "PROBE-BAD " : "PROBE-OK  ") + mode.name + " title=" + (t === 0 ? "long" : "short") +
                        " content=" + grid.width.toFixed(1) + " columns=" + grid.columns +
                        " flow.width=" + actions.width.toFixed(2) + " rowWidth=" + actions.rowWidth.toFixed(2) +
                        " lines=" + lineCount + " title.width=" + title.width.toFixed(1) +
                        " outside=[" + outside.join(",") + "]" + (titleOutside ? " TITLE-OUTSIDE" : ""))
            ++window.step
            tick.start()
        }
    }
    Component.onCompleted: tick.start()
}
