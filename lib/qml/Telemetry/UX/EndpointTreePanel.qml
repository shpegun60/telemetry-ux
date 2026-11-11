// Reusable presentation: the caller owns the provider and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models

Pane {
    id: panel
    objectName: "endpointTreePane"
    required property var provider
    // Evaluate the provider's O(1) current-index projection once per panel,
    // rather than querying it separately for every visible delegate.
    readonly property var selectedEndpointIndex: provider.selectedEndpointIndex
    readonly property var searchSource: provider.endpoints
    property var searchResults: []
    property var searchStack: []
    property bool searchRunning: false
    property bool searchLimited: false
    property int searchGeneration: 0
    readonly property int searchLimit: 500
    readonly property int searchDepthLimit: 64

    // DelegateModel exposes named roles independently of their numeric IDs.
    // It does not create visual delegates for this traversal. Results retain
    // source indexes only until the next model change or search cancellation.
    function cancelSearch() {
        ++searchGeneration
        searchTimer.stop()
        searchRunning = false
        searchResults = []
        searchStack = []
    }
    function scheduleSearch() {
        cancelSearch()
        searchLimited = false
        if (searchInput.text.trim().length > 0 && searchSource) {
            searchRunning = true
            searchTimer.interval = 50
            searchTimer.restart()
        }
    }
    function scanSearch() {
        if (!searchSource || searchInput.text.trim().length === 0) {
            cancelSearch()
            return
        }
        const needle = searchInput.text.trim().toLocaleLowerCase().replace(/\s*\/\s*/g, ".")
        const generation = searchGeneration
        if (searchStack.length === 0)
            searchStack = [{ parent: searchSource.index(-1, -1), row: 0, path: "", depth: 0 }]
        let visited = 0
        let matches = searchResults.slice()
        while (searchStack.length && visited < 128 && matches.length < searchLimit) {
            const frame = searchStack[searchStack.length - 1]
            searchModel.rootIndex = frame.parent
            if (generation !== searchGeneration) return
            if (frame.row >= searchModel.count) {
                searchStack.pop()
                continue
            }
            const row = frame.row++
            const index = searchModel.modelIndex(row)
            const item = searchModel.items.get(row)
            if (generation !== searchGeneration) return
            ++visited
            if (!item || !item.model || !index.valid)
                continue
            const name = String(item.model.name === undefined ? "" : item.model.name)
            const endpoint = item.model.isEndpoint !== undefined && Boolean(item.model.isEndpoint)
            const children = searchSource.rowCount(index)
            if (generation !== searchGeneration) return
            const path = frame.path.length ? frame.path + " / " + name : name
            if (endpoint && path.toLocaleLowerCase().replace(/\s*\/\s*/g, ".").indexOf(needle) !== -1)
                matches.push({ path: path, index: index, generation: generation })
            if (children > 0 && frame.depth + 1 < searchDepthLimit)
                searchStack.push({ parent: index, row: 0, path: path, depth: frame.depth + 1 })
            else if (children > 0)
                searchLimited = true
        }
        searchResults = matches
        if (matches.length >= searchLimit) {
            searchLimited = true
            searchStack = []
        }
        if (searchStack.length) {
            searchTimer.interval = 0
            searchTimer.restart()
        } else {
            searchRunning = false
        }
    }
    onSearchSourceChanged: scheduleSearch()
    DelegateModel {
        id: searchModel
        model: panel.searchSource
        delegate: Item {}
    }
    Timer { id: searchTimer; repeat: false; onTriggered: panel.scanSearch() }
    palette.window: "#11151c"
    palette.windowText: "#e8edf5"
    palette.base: "#17202b"
    palette.alternateBase: "#1c2836"
    palette.text: "#e8edf5"
    palette.button: "#233349"
    palette.buttonText: "#e8edf5"
    palette.highlight: "#215a68"
    palette.highlightedText: "#ffffff"
    palette.placeholderText: "#718297"
    background: Rectangle { color: "#17202b"; radius: 10 }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        Label { textFormat: Text.PlainText; text: "DEVICE ENDPOINTS"; color: "#99abbf"; font.pixelSize: 11; font.letterSpacing: 1 }
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: searchInput
                objectName: "endpointSearch"
                Layout.fillWidth: true
                placeholderText: "Search endpoints"
                maximumLength: 256
                onTextChanged: panel.scheduleSearch()
            }
            ToolButton {
                text: "Clear"
                enabled: searchInput.text.length > 0
                onClicked: { searchInput.clear(); searchInput.forceActiveFocus() }
                Accessible.name: "Clear endpoint search"
            }
        }
        Label {
            objectName: "endpointSearchStatus"
            visible: searchInput.text.trim().length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#b0bfd0"
            text: panel.searchRunning ? "Searching…" :
                  panel.searchLimited ? "Search limited; refine search" :
                  panel.searchResults.length === 0 ? "No matching endpoints" :
                  panel.searchResults.length + " matching endpoints"
        }
        TreeView {
            id: endpointTree
            objectName: "endpointTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: searchInput.text.trim().length === 0
            clip: true
            model: panel.provider.endpoints
            // Current keyboard row is a navigation cursor, not operation
            // selection. Only an explicit click/Enter asks the provider.
            activeFocusOnTab: true
            selectionModel: ItemSelectionModel { model: endpointTree.model }
            Keys.onReturnPressed: function(event) {
                const index = endpointTree.selectionModel.currentIndex
                if (index.valid)
                    panel.provider.selectEndpoint(index)
            }
            Keys.onEnterPressed: function(event) {
                const index = endpointTree.selectionModel.currentIndex
                if (index.valid)
                    panel.provider.selectEndpoint(index)
            }
            columnWidthProvider: function() { return Math.max(1, endpointTree.width) }
            onWidthChanged: Qt.callLater(function() { if (endpointTree) endpointTree.forceLayout() })
            delegate: TreeViewDelegate {
                id: endpointDelegate
                objectName: "endpointRow"
                required property string name
                required property string detail
                required property bool isEndpoint
                implicitWidth: endpointTree.width
                implicitHeight: 38
                text: name
                // Optional for third-party providers. Selection stays owned by the
                // provider, including when it refuses a click while busy.
                highlighted: isEndpoint && panel.selectedEndpointIndex !== undefined
                    && endpointTree.modelIndex(Qt.point(0, row)) === panel.selectedEndpointIndex
                Rectangle {
                    objectName: "endpointFocusIndicator"
                    anchors.fill: parent
                    color: "transparent"
                    border.color: "#8ce6d3"
                    border.width: 2
                    radius: 3
                    visible: endpointTree.activeFocus && endpointDelegate.current
                }
                PlainToolTip {
                    objectName: "endpointToolTip"
                    visible: endpointDelegate.hovered && endpointDelegate.detail.length > 0
                    text: endpointDelegate.detail
                }
                // Basic TreeViewDelegate reads model.display directly.
                // A retired model may have no role data before its reset.
                contentItem: Label {
                    textFormat: Text.PlainText
                    text: endpointDelegate.name
                    elide: Text.ElideRight
                    color: endpointDelegate.highlighted
                        ? endpointDelegate.palette.highlightedText
                        : endpointDelegate.palette.buttonText
                }
                onClicked: {
                    if (isEndpoint)
                        panel.provider.selectEndpoint(endpointTree.modelIndex(Qt.point(0, row)))
                }
            }
            ScrollBar.vertical: ScrollBar {}
            Component.onCompleted: Qt.callLater(function() {
                if (!endpointTree) return
                endpointTree.forceLayout()
                endpointTree.expandRecursively(0, 2)
            })
        }
        ListView {
            id: searchList
            objectName: "endpointSearchResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: searchInput.text.trim().length > 0
            clip: true
            model: panel.searchResults
            delegate: ItemDelegate {
                required property var modelData
                objectName: "endpointSearchResult"
                width: searchList.width
                text: modelData.path
                highlighted: panel.selectedEndpointIndex !== undefined &&
                             modelData.index === panel.selectedEndpointIndex
                function activateResult() {
                    if (modelData.generation === panel.searchGeneration && modelData.index.valid)
                        panel.provider.selectEndpoint(modelData.index)
                }
                onClicked: activateResult()
                Keys.onReturnPressed: function(event) { activateResult(); event.accepted = true }
                Keys.onEnterPressed: function(event) { activateResult(); event.accepted = true }
                contentItem: Label { textFormat: Text.PlainText; text: parent.text; elide: Text.ElideLeft; color: parent.highlighted ? "white" : "#e8edf5" }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        Label { textFormat: Text.PlainText; text: "Commands and Services have an explicit Request editor. Each action submits one request."; color: "#99abbf"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
    }
    Connections {
        target: panel.provider.endpoints
        ignoreUnknownSignals: true
        function onModelAboutToBeReset() { panel.cancelSearch() }
        function onModelReset() {
            panel.scheduleSearch()
            Qt.callLater(function() { if (endpointTree) endpointTree.expandRecursively(0, 2) })
        }
        function onRowsAboutToBeRemoved() { panel.cancelSearch() }
        function onRowsRemoved() { panel.scheduleSearch() }
        function onRowsAboutToBeInserted() { panel.cancelSearch() }
        function onRowsInserted() { panel.scheduleSearch() }
        function onRowsAboutToBeMoved() { panel.cancelSearch() }
        function onRowsMoved() { panel.scheduleSearch() }
        function onLayoutAboutToBeChanged() { panel.cancelSearch() }
        function onLayoutChanged() { panel.scheduleSearch() }
        function onDataChanged() { panel.scheduleSearch() }
        function onDestroyed() { panel.cancelSearch() }
    }
}
