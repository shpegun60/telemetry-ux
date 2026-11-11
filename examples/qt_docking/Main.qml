// Optional Qt Quick example using the installed Telemetry connection and panels.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX
import com.kdab.dockwidgets 2.0 as KDDW

ApplicationWindow {
    id: window
    width: 1160
    height: 800
    visible: true
    title: "Telemetry — QML docking experiment"
    color: "#11151c"
    property bool restartLayoutRestored: false
    property bool restartLayoutSaved: false
    property bool layoutReady: false
    property bool connectionRequested: false
    palette.window: "#11151c"
    palette.windowText: "#e8edf5"
    palette.base: "#17202b"
    palette.text: "#e8edf5"
    palette.button: "#233349"
    palette.buttonText: "#e8edf5"
    palette.highlight: "#215a68"
    palette.highlightedText: "#ffffff"

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label { text: "Telemetry"; font.bold: true; Layout.leftMargin: 12 }
            Label { text: connection.ready ? "Connected" : connection.connecting ? "Connecting" : "Disconnected"; color: connection.ready ? "#83e1ce" : "#b0bfd0" }
            Button {
                id: connectionAction
                objectName: "connectionAction"
                text: "Connection…"
                onClicked: window.requestConnection()
            }
            Button {
                id: errorAction
                objectName: "connectionErrorAction"
                visible: !connection.ready && !connection.connecting && connection.lastError.length > 0
                text: "Connection error · Open Connection"
                onClicked: window.requestConnection()
            }
            Item { Layout.fillWidth: true }
            Label { text: "Read-only example"; color: "#b0bfd0"; Layout.rightMargin: 12 }
        }
    }
    menuBar: MenuBar {
        Menu {
            title: "Panels"
            Action { text: "Connection"; onTriggered: window.requestConnection() }
            Action { text: "Device tree"; onTriggered: window.showDock(treeDock) }
            Action { text: "Selected endpoint"; onTriggered: window.showDock(endpointDock) }
            Action { text: "Compare endpoint"; onTriggered: window.showDock(compareDock) }
            Action { text: "Watchlist"; onTriggered: window.showDock(watchDock) }
            MenuItem { text: "Trend"; onTriggered: window.showDock(trendDock) }
            Action { text: "Batch"; onTriggered: window.showDock(batchDock) }
            Action { text: "Example panel"; onTriggered: window.showDock(exampleDock) }
            MenuSeparator {}
            Action { text: "Reset layout"; onTriggered: window.resetLayout() }
        }
    }
    KDDW.DockingArea {
        id: docks
        anchors.fill: parent
        uniqueName: "telemetry-qml-experiment"
        KDDW.DockWidget {
            id: connectionDock
            uniqueName: "telemetry.connection"
            title: "Connection"
            Rectangle {
                property var kddockwidgets_min_size: Qt.size(260, 220)
                anchors.fill: parent
                color: "#17202b"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10
                    Label { text: "Device connection"; color: "#e8edf5"; font.pixelSize: 18; font.bold: true }
                    Label { text: "Reference TCP profile"; color: "#b0bfd0" }
                    RowLayout {
                        Label { text: "Host"; color: "#e8edf5" }
                        TextField { id: hostname; objectName: "connectionHost"; text: "127.0.0.1"; enabled: !connection.ready && !connection.connecting; Layout.fillWidth: true }
                    }
                    RowLayout {
                        Label { text: "Port"; color: "#e8edf5" }
                        SpinBox { id: port; objectName: "connectionPort"; locale: Qt.locale("C"); from: 1; to: 65535; value: 5577; editable: true; enabled: !connection.ready && !connection.connecting; Layout.fillWidth: true }
                    }
                    RowLayout {
                        Button { text: "Connect"; enabled: !connection.ready && !connection.connecting && hostname.text.trim().length > 0; onClicked: connection.connectTcpReference(hostname.text.trim(), port.value) }
                        Button { text: "Disconnect"; enabled: connection.ready || connection.connecting; onClicked: connection.disconnect() }
                        Item { Layout.fillWidth: true }
                    }
                    Label { text: connection.lastError; textFormat: Text.PlainText; visible: text.length > 0; color: "#f2b96a"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: "Closing this panel keeps the connection active."; color: "#b0bfd0"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        KDDW.DockWidget {
            id: treeDock
            uniqueName: "telemetry.tree"
            title: "Device tree"
            Rectangle {
                property var kddockwidgets_min_size: Qt.size(230, 220)
                anchors.fill: parent
                color: "#11151c"
                EndpointTreePanel { id: treePanel; anchors.fill: parent; provider: device }
            }
        }
        KDDW.DockWidget {
            id: endpointDock
            uniqueName: "telemetry.endpoint"
            title: "Selected endpoint"
            Rectangle {
                property var kddockwidgets_min_size: Qt.size(380, 220)
                anchors.fill: parent
                color: "#11151c"
                EndpointPanel { id: endpointPanel; anchors.fill: parent; provider: device }
            }
        }
        KDDW.DockWidget {
            id: watchDock
            uniqueName: "telemetry.watch"
            title: "Watchlist"
            Rectangle {
                property var kddockwidgets_min_size: Qt.size(400, 240)
                anchors.fill: parent
                color: "#11151c"
                Loader {
                    id: watchLoader
                    anchors.fill: parent
                    active: device.watchlist !== undefined && device.watchlist !== null
                    sourceComponent: Component { WatchlistPanel { provider: device } }
                }
            }
        }
        KDDW.DockWidget {
            id: compareDock
            uniqueName: "telemetry.compare"
            title: "Compare endpoint"
            Rectangle {
                property var kddockwidgets_min_size: Qt.size(560, 450)
                anchors.fill: parent
                color: "#11151c"
                SplitView {
                    anchors.fill: parent
                    // The two panes need at least 220 + 360 pixels plus a handle.
                    orientation: width < 600 ? Qt.Vertical : Qt.Horizontal
                    EndpointTreePanel {
                        id: compareTree
                        SplitView.preferredWidth: 240
                        SplitView.preferredHeight: 230
                        SplitView.minimumWidth: 220
                        SplitView.minimumHeight: 160
                        provider: compareDevice
                    }
                    EndpointPanel {
                        id: comparePanel
                        objectName: "compareEndpointPanel"
                        SplitView.fillWidth: true
                        SplitView.fillHeight: true
                        SplitView.minimumWidth: 360
                        SplitView.minimumHeight: 220
                        provider: compareDevice
                    }
                }
            }
        }
        KDDW.DockWidget {
            id: trendDock
            uniqueName: "telemetry.trend"
            title: "Trend"
            Rectangle {
                property var kddockwidgets_min_size: Qt.size(400, 240)
                anchors.fill: parent
                color: "#11151c"
                Loader { anchors.fill: parent; source: "qrc:/TrendContent.qml" }
            }
        }
        KDDW.DockWidget {
            id: batchDock
            uniqueName: "telemetry.batch"
            title: "Batch"
            Rectangle {
                anchors.fill: parent
                color: "#11151c"
                Loader {
                    id: batchLoader
                    anchors.fill: parent
                    active: device.batchItems !== undefined && device.batchItems !== null
                    sourceComponent: Component { BatchPanel { provider: device } }
                }
            }
        }
        KDDW.DockWidget {
            id: exampleDock
            uniqueName: "example.notes"
            title: "Example panel"
            Rectangle {
                anchors.fill: parent
                color: "#17202b"
                Label { anchors.centerIn: parent; text: "Your own QML component"; color: "#b0bfd0" }
            }
        }
        Component.onCompleted: {
            docks.addDockWidget(endpointDock, KDDW.KDDockWidgets.Location_OnLeft)
            docks.addDockWidget(treeDock, KDDW.KDDockWidgets.Location_OnLeft, endpointDock, Qt.size(240, 600))
            docks.addDockWidget(connectionDock, KDDW.KDDockWidgets.Location_OnRight, endpointDock, Qt.size(320, 600))
            docks.addDockWidget(watchDock, KDDW.KDDockWidgets.Location_OnBottom, endpointDock, Qt.size(560, 220))
            watchDock.addDockWidgetAsTab(batchDock)
            watchDock.addDockWidgetAsTab(exampleDock)
            watchDock.addDockWidgetAsTab(compareDock)
            watchDock.addDockWidgetAsTab(trendDock)
            watchDock.setAsCurrentTab()
            connectionDock.close()
            compareDock.close()
            trendDock.close()
            Qt.callLater(function() {
                // Establish the default independently of an early click.
                connectionDock.close()
                layout.saveToFile(defaultFile)
                if (savedLayoutExists) {
                    restartLayoutRestored = layout.restoreFromFile(savedFile)
                    if (!restartLayoutRestored)
                        layout.restoreFromFile(defaultFile)
                }
                // Preserve a request made while the saved layout was loading.
                if (connectionRequested)
                    window.showDock(connectionDock)
                layoutReady = true
            })
        }
    }
    KDDW.LayoutSaver { id: layout }
    function saveLayoutBeforeClose() {
        // Preserve a previous layout if the application closes before restore.
        if (!layoutReady)
            return true
        restartLayoutSaved = layout.saveToFile(savedFile)
        return restartLayoutSaved
    }
    function requestConnection() {
        connectionRequested = true
        showDock(connectionDock)
    }
    function resetLayout() {
        if (!layoutReady || !layout.restoreFromFile(defaultFile))
            return false
        connectionDock.close()
        return true
    }
    function showDock(dock) {
        dock.show()
        dock.setAsCurrentTab()
    }
    // Layout checks (see tests/check_layout.py): a state that differs from the
    // default in every saved aspect, so restoring nothing cannot pass.
    function prepareNonDefaultLayout() {
        requestConnection()
        showDock(compareDock)
        treeDock.isFloating = true
        showDock(trendDock)
        trendDock.isFloating = true
        return connectionDock.isOpen && compareDock.isOpen && treeDock.isOpen && treeDock.isFloating &&
               trendDock.isOpen && trendDock.isFloating
    }
    function layoutState() {
        return {
            restored: restartLayoutRestored,
            connection: connectionDock.isOpen,
            compare: compareDock.isOpen,
            tree: treeDock.isOpen,
            treeFloating: treeDock.isFloating,
            trend: trendDock.isOpen,
            trendFloating: trendDock.isFloating,
            endpoint: endpointDock.isOpen,
            watch: watchDock.isOpen
        }
    }
    // Runs only when requested by the host's isolated smoke test.
    function smokeCheck() {
        if (!layoutReady || connection.ready || connection.connecting || connectionDock.isOpen)
            return false
        requestConnection()
        if (!connectionDock.isOpen) return false
        treeDock.isFloating = true
        if (!treeDock.isFloating || !treeDock.isOpen) return false
        treeDock.isFloating = false
        if (treeDock.isFloating || !treeDock.isOpen) return false
        return resetLayout() && !connectionDock.isOpen && treeDock.isOpen &&
               endpointDock.isOpen && watchDock.isOpen && !compareDock.isOpen
    }
}
