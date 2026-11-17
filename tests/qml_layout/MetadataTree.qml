// The definition table Details shows before a Field is read, over a fake provider
// whose `definitions` is the real filter's twin (MetadataTree.cpp). MIT.
import QtQuick
import QtQuick.Window
import Telemetry.UX

Window {
    id: win
    objectName: "metadataWindow"
    visible: true
    width: 1000
    height: 800

    QtObject {
        id: fake
        objectName: "metadataProvider"
        property string selection: control.selection
        property var definitionRootIndex: control.rootIndex
        readonly property var definitions: control.definitions
        readonly property var values: control.values
        readonly property var responses: control.values
        property bool connected: true
        property bool busy: false
        property bool editing: false
        property bool commandSelected: false
        property bool serviceSelected: false
        property bool allowMutations: false
        property bool canRefresh: true
        property var selectionInfo: ({ readable: true, writable: true, typeText: "struct (3)" })
        property string serviceResponseEndpoint: ""
        property string writeOutcome: ""
        property string commandOutcome: ""
        property string serviceOutcome: ""
        signal readFinished(bool published)
        function refresh() { }
    }
    FoldMemory { id: hostMemory }
    Row {
        anchors.fill: parent
        EndpointPanel {
            objectName: "detailsPanel"
            width: parent.width / 2; height: parent.height
            provider: fake
        }
        // The Batch item's pane: the same panel with the host's folds.
        EndpointPanel {
            objectName: "batchItemPanel"
            width: parent.width / 2; height: parent.height
            provider: fake
            foldMemory: hostMemory
            foldScope: "item:1"
            infoPrefix: qsTr("Item 1 · ")
        }
    }
    function find(item, name, out) {
        if (item.objectName === name)
            out.push(item)
        const kids = item.data ? item.data : []
        for (let i = 0; i < kids.length; ++i)
            find(kids[i], name, out)
        return out
    }
    // What a table shows: one entry per visible row of the named panel, top down.
    function table(panelName) {
        const panel = find(win.contentItem, panelName, [])[0]
        const rows = find(panel, "endpointRow", []).filter(function(r) { return r.visible && r.width > 0 })
        rows.sort(function(a, b) { return a.mapToItem(panel, 0, 0).y - b.mapToItem(panel, 0, 0).y })
        return rows.map(function(r) {
            const type = find(r, "detailCellType", [])[0]
            const value = find(r, "metadataValueLabel", [])[0]
            return { name: r.cellName, type: r.cellType, typeShown: !!type && type.visible && type.width > 0,
                     value: value ? value.text : "", depth: r.depth, expanded: r.expanded }
        })
    }
}
