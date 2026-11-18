// The Details layout spec's checks (§11) and the action feedback's: Details
// and the Batch over host-owned fake providers whose tables are real
// ValueModels (DetailsLayout.cpp). The window's font follows Type.body, as
// the client's zoom sets both. MIT.
import QtQuick
import QtQuick.Controls
import Telemetry.UX

ApplicationWindow {
    id: win
    objectName: "detailsLayoutWindow"
    visible: true
    width: 560
    height: 700
    color: Theme.surfaceWindow
    font.pointSize: Type.body
    // "details" or "batch": the panel the window shows.
    property string mode: "details"
    // What the checks compare colours and sizes with.
    readonly property var theme: Theme
    readonly property var type: Type
    EndpointPanel {
        id: details
        objectName: "details"
        visible: win.mode === "details"
        anchors.fill: parent
        provider: detailsProvider
    }
    BatchPanel {
        id: batch
        objectName: "batch"
        visible: win.mode === "batch"
        anchors.fill: parent
        provider: batchProvider
    }
    // Every visible Label (and Text) under an item whose text holds words.
    function labelsWith(item, words, out) {
        if (item.visible === false)
            return out
        if (typeof item.text === "string" && item.text.indexOf(words) >= 0 && item.opacity > 0
            && item.width > 0 && item.height > 0 && item.objectName !== "endpointStatusTime"
            && item.objectName !== "batchStatusTime")
            out.push(item)
        const kids = item.children ? item.children : []
        for (let i = 0; i < kids.length; ++i)
            labelsWith(kids[i], words, out)
        return out
    }
    function countLabels(words) {
        return labelsWith(win.contentItem, words, []).length
    }
}
