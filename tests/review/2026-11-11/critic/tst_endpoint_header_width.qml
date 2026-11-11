// Critic probe (review 2026-11-11): EndpointPanel header actions in the four
// header modes at several dock widths. PR #47 claims 280 px; the shipped check
// (tests/qml_layout/EndpointStates.qml) runs at 360 px only. Run with
// qmltestrunner from a directory that also holds copies of the library QML
// files (EndpointPanel.qml, PlainToolTip.qml, FieldEditor.qml, ...).
import QtQuick
import QtTest

Item {
    id: root
    width: 800
    height: 700
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
        function beginEdit() { editing = true }
        function cancelEdit() { editing = false }
        function apply() {}
        function refresh() {}
        function executeCommand() {}
        function callService() {}
    }
    Component {
        id: panelComponent
        EndpointPanel { provider: fake; height: 640 }
    }
    function find(item, name) {
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = find(item.children[i], name)
            if (found) return found
        }
        return null
    }
    TestCase {
        name: "EndpointHeaderWidth"
        when: windowShown
        function data_widths() { return [{tag: "360", w: 360}, {tag: "320", w: 320}, {tag: "300", w: 300}, {tag: "280", w: 280}] }
        function test_modes_data() { return data_widths() }
        function test_modes(row) {
            const panel = createTemporaryObject(panelComponent, root, {width: row.w})
            verify(panel)
            const modes = [
                {name: "view", editing: false, command: false, service: false},
                {name: "edit", editing: true, command: false, service: false},
                {name: "command", editing: false, command: true, service: false},
                {name: "service", editing: false, command: false, service: true}]
            const names = ["editButton", "cancelEditButton", "applyButton", "refreshButton", "executeButton", "callServiceButton"]
            let failures = []
            for (const m of modes) {
                fake.editing = m.editing; fake.commandSelected = m.command; fake.serviceSelected = m.service
                wait(50)
                const title = find(panel, "endpointSelectionTitle")
                const actions = find(panel, "endpointSelectionActions")
                const t = title.mapToItem(panel, 0, 0, title.width, title.height)
                const a = actions.mapToItem(panel, 0, 0, actions.width, actions.height)
                if (t.y + t.height > a.y + 1) failures.push(m.name + ": title overlaps actions")
                let rects = []
                for (const n of names) {
                    const b = find(panel, n)
                    if (!b || !b.visible) continue
                    const r = b.mapToItem(panel, 0, 0, b.width, b.height)
                    rects.push({n: n, r: r})
                    if (r.x < -1 || r.x + r.width > panel.width + 1)
                        failures.push(m.name + ": " + n + " x=" + r.x.toFixed(1) + " right=" + (r.x + r.width).toFixed(1) + " panel=" + panel.width)
                    if (r.x + r.width > panel.width - panel.rightPadding + 1)
                        console.log("w=" + row.w, m.name, n, "enters right padding: right=" + (r.x + r.width).toFixed(1) + " content edge=" + (panel.width - panel.rightPadding))
                }
                console.log("w=" + row.w, m.name, rects.map(q => q.n + "[" + q.r.x.toFixed(0) + "," + (q.r.x + q.r.width).toFixed(0) + "]").join(" "))
            }
            fake.editing = false; fake.commandSelected = false; fake.serviceSelected = false
            compare(failures.join("; "), "")
        }
    }
}
