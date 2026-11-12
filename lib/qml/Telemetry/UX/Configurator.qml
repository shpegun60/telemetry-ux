// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// Presentation only: the caller owns this provider, its models and all I/O.
Control {
    id: configurator
    required property var provider
    property bool watchlistVisible: true
    property bool batchVisible: true
    property Component headerComponent: null
    property Component footerComponent: null
    padding: 0
    background: Rectangle { color: configurator.palette.window }
    // ScrollView's width depends on the host allocation. Keep the preferred
    // width independent so an enclosing RowLayout cannot feed its own width
    // back into its size hints (a polish loop on Qt 6.4).
    implicitWidth: 640
    implicitHeight: contentItem.implicitHeight
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

    contentItem: ScrollView {
        id: workspaceScroll
        objectName: "configuratorScroll"
        clip: true
        contentWidth: availableWidth
        contentHeight: workspace.height
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: workspace
            width: workspaceScroll.availableWidth
            height: implicitHeight
            spacing: Type.groupSpacing
            readonly property int visibleRows: 2 + Number(headerLoader.visible) + Number(watchLoader.visible)
                + Number(batchLoader.visible) + Number(footerLoader.visible)
            readonly property real auxiliaryHeight: (headerLoader.visible ? headerLoader.implicitHeight : 0)
                + (watchLoader.visible ? watchLoader.implicitHeight : 0)
                + (batchLoader.visible ? batchLoader.implicitHeight : 0)
                + connectionStatus.implicitHeight + (footerLoader.visible ? footerLoader.implicitHeight : 0)
                + spacing * (visibleRows - 1)
            Loader { id: headerLoader; Layout.fillWidth: true; active: configurator.headerComponent !== null; visible: active; sourceComponent: configurator.headerComponent }
            SplitView {
                objectName: "primaryWorkspace"
                Layout.fillWidth: true
                // Advanced panels may grow below this workspace, never consume it.
                Layout.minimumHeight: 400
                Layout.preferredHeight: Math.max(400, configurator.availableHeight - workspace.auxiliaryHeight)
                orientation: Qt.Horizontal
                EndpointTreePanel {
                    provider: configurator.provider
                    palette: configurator.palette
                    SplitView.preferredWidth: 300
                    SplitView.minimumWidth: 220
                }
                EndpointPanel {
                    provider: configurator.provider
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 360
                }
            }
            Loader {
                id: watchLoader
                Layout.fillWidth: true
                Layout.preferredHeight: item ? item.implicitHeight : 0
                Layout.minimumHeight: Layout.preferredHeight
                active: configurator.provider.watchlist !== undefined && configurator.provider.watchlist !== null
                visible: configurator.watchlistVisible && active
                sourceComponent: Component { WatchlistPanel { provider: configurator.provider; palette: configurator.palette } }
            }
            Loader {
                id: batchLoader
                Layout.fillWidth: true
                Layout.preferredHeight: item ? item.implicitHeight : 0
                Layout.minimumHeight: Layout.preferredHeight
                active: configurator.provider.batchItems !== undefined && configurator.provider.batchItems !== null
                visible: configurator.batchVisible && active
                sourceComponent: Component { BatchPanel { provider: configurator.provider } }
            }
            Label {
                id: connectionStatus
                textFormat: Text.PlainText
                objectName: "statusLabel"
                text: configurator.provider.statusText
                color: configurator.provider.connected ? "#83e1ce" : "#99abbf"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Loader { id: footerLoader; Layout.fillWidth: true; active: configurator.footerComponent !== null; visible: active; sourceComponent: configurator.footerComponent }
        }
    }
    Connections {
        target: configurator.Window.window
        function onActiveFocusItemChanged() {
            const focused = configurator.Window.window.activeFocusItem
            let ancestor = focused
            while (ancestor && ancestor !== workspace)
                ancestor = ancestor.parent
            if (!ancestor)
                return
            // Keyboard navigation must reveal controls below the main workspace.
            const viewport = workspaceScroll.contentItem
            const position = focused.mapToItem(workspace, 0, 0)
            let offset = viewport.contentY
            if (position.y < offset)
                offset = position.y
            else if (position.y + focused.height > offset + viewport.height)
                offset = position.y + focused.height - viewport.height
            viewport.contentY = Math.max(0, Math.min(offset, viewport.contentHeight - viewport.height))
        }
    }
}
