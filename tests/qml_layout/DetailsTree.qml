// Details' value tree over a fake provider whose models are
// QStandardItemModels with the roles of the real ValueModel (DetailsTree.cpp). MIT.
import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    id: win
    objectName: "treeWindow"
    visible: true
    width: 560
    height: 900

    ListModel { id: emptyResponses }
    QtObject {
        id: fake
        objectName: "treeProvider"
        property string selection: "device.Config"
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
        property var values: fieldModel
        property var responses: emptyResponses
        signal readFinished(bool published)
        // WL-9 C4: a device that refuses every Apply, said once in the status
        // line; the draft stays, the Field is read once, no cell is blamed.
        property var detailsStatus: null
        property int applies: 0
        property int reads: 0
        // The first change opens a draft, as the real provider's (only in the
        // WL-9 checks: the earlier ones edit with no draft).
        property bool draftOpens: false
        function refresh() { ++reads }
        function setScalarText(index, text) {
            const taken = values.setScalar(index, text)
            if (taken && draftOpens) {
                editing = true
                detailsStatus = null
            }
            return taken
        }
        function apply() {
            ++applies
            detailsStatus = { text: "Apply refused " + Qt.formatTime(new Date(), "HH:mm:ss")
                                    + " · the device does not take these values", failed: true, tip: "" }
            refresh()
            values.refuse()
        }
        function cancelEdit() {
            values.cancelDraft()
            editing = false
            detailsStatus = null
        }
    }
    EndpointPanel {
        id: panel
        objectName: "treePanel"
        anchors.fill: parent
        provider: fake
    }
    // The panel keeps its own folds; a host's (the Batch's) is given.
    FoldMemory { id: hostMemory }
    property var hostFolds: hostMemory

    function find(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        const kids = item.data ? item.data : []
        for (let i = 0; i < kids.length; ++i) {
            const found = find(kids[i], name)
            if (found)
                return found
        }
        return null
    }
    function findAll(item, name, out) {
        if (!item)
            return out
        if (item.objectName === name)
            out.push(item)
        const kids = item.data ? item.data : []
        for (let i = 0; i < kids.length; ++i)
            findAll(kids[i], name, out)
        return out
    }
    function gapPixels() { return Type.columnGap }
    function bodyPixels() { return Type.bodyPixels }
    function indentPixels() { return Type.indent }
    function paddingPixels() { return Type.padding }
    function glyphPixels() { return Type.iconSize + Type.spacing }
    function setBody(points) { Type.body = points }
    function nameFloorPixels() { return Type.nameFloor }
    function valueSeen() { return panel.valueSeen }
    function panelWidth() { return panel.width }
    function openRowMenu(r) { panel.openMenu(view(), r) }
    function view() { return find(panel, "valueTree") }
    function scene(item, x, y) { return item.mapToItem(null, x === undefined ? 0 : x, y === undefined ? 0 : y) }
    function hostScope(scope) {
        panel.foldMemory = hostFolds
        panel.foldScope = scope
    }
    function reread() { fake.values.reread(); fake.readFinished(true) }
    function use(kind) {
        if (kind === "field") {
            fake.commandSelected = false; fake.serviceSelected = false
            fake.selection = "device.Config"; fake.values = fieldModel
        } else if (kind === "deep") {
            fake.commandSelected = false; fake.serviceSelected = false
            fake.selection = "device.Deep"; fake.values = deepModel
        } else if (kind === "service") {
            fake.commandSelected = false; fake.serviceSelected = true
            fake.selection = "control.Scale"; fake.values = serviceModel
        } else if (kind === "single") {
            fake.commandSelected = false; fake.serviceSelected = true
            fake.selection = "control.Configure"; fake.values = singleModel
        } else if (kind === "wide") {
            fake.commandSelected = false; fake.serviceSelected = false
            fake.selection = "device.Wide"; fake.values = wideModel
        } else if (kind === "other") {
            fake.commandSelected = false; fake.serviceSelected = false
            fake.selection = "device.Other"; fake.values = fieldModel
        } else if (kind === "byte") {
            fake.commandSelected = false; fake.serviceSelected = false
            fake.selection = "device.Bytes"; fake.values = byteModel
        }
        fake.readFinished(true)
    }
    function resizeTo(w) { win.width = w }
    function expandAllAsTheClientDoes() { const v = view(); v.expand(0); v.forceLayout(); v.expandRecursively(); v.forceLayout() }
    function rowCount() { return view().rows }
    function row(r) {
        const v = view()
        const it = v.itemAtCell(Qt.point(0, r))
        if (!it)
            return null
        const name = find(it, "detailCellName")
        const type = find(it, "detailCellType")
        const cell = find(it, "valueCell")
        const text = find(it, "valueText")
        const summary = find(it, "foldedSummary")
        const sumTip = find(it, "foldedSummaryToolTip")
        const tip = find(it, "valueToolTip")
        const marks = findAll(it, "unsentMark", [])
        const guides = findAll(it, "indentGuide", [])
        const info = {
            row: r, depth: v.depth(r), treeX: scene(v).x, height: it.height, itemWidth: it.width,
            name: name.text, nameX: name.x, nameW: name.width, weight: name.font.weight,
            heading: name.font.weight === Type.headingWeight,
            chevronVisible: it.indicator ? it.indicator.visible : false, chevronX: it.indicator ? it.indicator.x : -1,
            type: type.typeText, typeSceneX: scene(type).x,
            expanded: it.expanded, hasChildren: it.hasChildren,
            valueText: text && text.visible ? text.text : "",
            summary: summary && summary.visible ? summary.text : "",
            summaryRight: summary ? scene(summary, summary.width).x : -1,
            summaryLeft: summary ? scene(summary, summary.width - summary.contentWidth).x : -1,
            summaryTip: sumTip ? sumTip.text : "",
            nameTextEnd: scene(name, name.contentWidth).x,
            cellSceneX: cell ? scene(cell).x : -1, cellW: cell ? cell.width : -1,
            cellItem: !!(cell && cell.item),
            unsentMark: marks.length > 0 && marks[0].visible,
            pathTip: tip ? tip.text : "",
            desc: it.accessibleText,
            guides: guides.filter(g => g.visible).map(g => ({ x: g.x, w: g.width, h: g.height, color: String(g.color), active: g.active })),
            outline: String(Theme.outline), outlineVariant: String(Theme.outlineVariant),
            editor: (function() { const e = find(it, "scalarEditor"); return e && e.visible })(),
            editorX: (function() { const e = find(it, "scalarEditor"); return e ? scene(e).x : -1 })(),
            editorW: (function() { const e = find(it, "scalarEditor"); return e ? e.width : -1 })(),
            selected: (function() { const e = find(it, "scalarEditor"); return e ? e.selectedText : "" })()
        }
        return info
    }
    function rows() {
        const out = []
        for (let r = 0; r < view().rows; ++r) {
            const info = row(r)
            if (info)
                out.push(info)
        }
        return out
    }
    function names() { return rows().map(r => r.name) }
    function rowByName(name) {
        const all = rows()
        for (const r of all)
            if (r.name === name)
                return r.row
        return -1
    }
    function rowByPath(path) {
        const v = view()
        for (let r = 0; r < v.rows; ++r)
            if (panel.pathOfRow(v, r) === path)
                return r
        return -1
    }
    function headerX(label) {
        const header = find(panel, "valueTableHeader")
        for (const child of header.children)
            if (child.text === label)
                return { x: scene(child).x, w: child.width }
        return null
    }
    function focusView() { view().forceActiveFocus() }
    function select(r) {
        const v = view()
        v.forceActiveFocus()
        panel.setCurrentRow(v, r)
    }
    function currentName() {
        const v = view()
        const r = v.currentIndexRow()
        return r < 0 ? "" : row(r).name
    }
    function menuInfo() {
        const menu = find(panel, "detailsRowMenu")
        return { visible: menu.visible, count: menu.count }
    }
    function menuTrigger(objectName) {
        const menu = find(panel, "detailsRowMenu")
        for (let i = 0; i < menu.count; ++i)
            if (menu.itemAt(i).objectName === objectName)
                menu.itemAt(i).triggered()
    }
    // Types into the cell of a row as the user does, through the cell's own setText.
    function editOffset(r) {
        const it = view().itemAtCell(Qt.point(0, r))
        return it.valueItem.setText("0.75")
    }
    function editorOpen(r) {
        const it = view().itemAtCell(Qt.point(0, r))
        return it.valueItem.begin()
    }
    function maxContentX() { return view().contentX }
    function contentWidth() { return view().contentWidth }
    function viewWidth() { return view().width }
    function headingCount(name) { return findAll(panel, name, []).filter(i => i.visible).length }
    // WL-9: the cell of a row, the status line, the provider.
    function cellAt(r) {
        const it = view().itemAtCell(Qt.point(0, r))
        return it ? it.valueItem : null
    }
    function rowItem(r) { return view().itemAtCell(Qt.point(0, r)) }
    function provider() { return fake }
    function statusText() { const l = find(panel, "endpointStatusLine"); return l && l.visible ? l.text : "" }
    function statusGlyph() { const g = find(panel, "endpointStatusGlyph"); return !!g && g.visible }
    function color(name) { return String(Theme[name]) }
    function valueTextTruncated(r) {
        const it = view().itemAtCell(Qt.point(0, r))
        const t = find(it, "valueText")
        return t ? t.truncated : false
    }
}
