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
        TreeView {
            id: endpointTree
            objectName: "endpointTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
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
        Label { textFormat: Text.PlainText; text: "Commands and Services have an explicit Request editor. Each action submits one request."; color: "#99abbf"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
    }
    Connections {
        target: panel.provider.endpoints
        function onModelReset() {
            Qt.callLater(function() { if (endpointTree) endpointTree.expandRecursively(0, 2) })
        }
    }
}
