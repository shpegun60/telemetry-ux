// Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Presentation only: the caller owns this provider, its models and all I/O.
Control {
    id: configurator
    required property var provider
    property bool watchlistVisible: true
    property Component headerComponent: null
    property Component footerComponent: null
    padding: 0
    background: Rectangle { color: configurator.palette.window }
    implicitWidth: contentItem.implicitWidth
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

    contentItem: ColumnLayout {
        spacing: 16
        Loader { Layout.fillWidth: true; active: configurator.headerComponent !== null; sourceComponent: configurator.headerComponent }
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            Pane {
                SplitView.preferredWidth: 300
                SplitView.minimumWidth: 220
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
                        model: configurator.provider.endpoints
                        columnWidthProvider: function() { return Math.max(1, endpointTree.width) }
                        onWidthChanged: Qt.callLater(function() { endpointTree.forceLayout() })
                        delegate: TreeViewDelegate {
                            id: endpointDelegate
                            objectName: "endpointRow"
                            required property string name
                            required property string detail
                            required property bool isEndpoint
                            implicitWidth: endpointTree.width
                            implicitHeight: 38
                            text: name
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
                                    configurator.provider.selectEndpoint(endpointTree.modelIndex(Qt.point(0, row)))
                            }
                        }
                        ScrollBar.vertical: ScrollBar {}
                        Component.onCompleted: Qt.callLater(function() {
                            endpointTree.forceLayout()
                            endpointTree.expandRecursively(0, 2)
                        })
                    }
                    Label { textFormat: Text.PlainText; text: "Commands and Services have an explicit Request editor. Each action submits one request."; color: "#99abbf"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
                }
            }
            EndpointPanel {
                provider: configurator.provider
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
            }
        }
        Pane {
            objectName: "watchlistPane"
            visible: configurator.watchlistVisible
            Layout.fillWidth: true
            Layout.preferredHeight: watchPanelExpanded.checked ? 220 : 54
            background: Rectangle { color: "#17202b"; radius: 10 }
            ColumnLayout {
                anchors.fill: parent
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        id: watchPanelExpanded
                        objectName: "watchlistExpanded"
                        text: "Watchlist " + configurator.provider.watchCount + "/32"
                        checked: false
                    }
                    Button {
                        objectName: "addWatchButton"
                        text: "Add selected Field"
                        enabled: configurator.provider.canAddWatch && !configurator.provider.watchActive
                        onClicked: {
                            if (configurator.provider.addWatch())
                                watchPanelExpanded.checked = true
                        }
                    }
                    Button {
                        objectName: "refreshWatchButton"
                        text: "Read once"
                        enabled: configurator.provider.connected && configurator.provider.watchCount > 0 && !configurator.provider.watchActive
                        onClicked: configurator.provider.refreshWatch()
                    }
                    Button {
                        objectName: "startWatchButton"
                        text: configurator.provider.watchActive ? "Stop" : "Start polling"
                        enabled: configurator.provider.watchActive || (configurator.provider.connected && configurator.provider.watchCount > 0)
                        onClicked: configurator.provider.watchActive ? configurator.provider.stopWatch() : configurator.provider.startWatch()
                    }
                    Label { textFormat: Text.PlainText; text: configurator.provider.watchPending ? "Reading…" : configurator.provider.watchStatus; color: "#99abbf"; elide: Text.ElideRight; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: 330 }
                }
                RowLayout {
                    visible: watchPanelExpanded.checked
                    Layout.fillWidth: true
                    ComboBox {
                        objectName: "watchMode"
                        Layout.preferredWidth: 210
                        model: ["ReadMany", "Individual FieldRead"]
                        currentIndex: configurator.provider.watchIndividual ? 1 : 0
                        enabled: !configurator.provider.watchActive
                        onActivated: configurator.provider.watchIndividual = currentIndex === 1
                    }
                    Label { textFormat: Text.PlainText; text: "Frequency"; color: "#99abbf" }
                    SpinBox {
                        objectName: "watchFrequency"
                        from: 1
                        to: 10
                        value: Math.max(1, Math.min(10, Math.round(1000 / configurator.provider.watchIntervalMs)))
                        enabled: !configurator.provider.watchActive
                        onValueModified: configurator.provider.watchIntervalMs = Math.round(1000 / value)
                    }
                    Label { textFormat: Text.PlainText; text: "Hz · scalar Fields only · sequential samples"; color: "#99abbf"; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
                    Button {
                        objectName: "clearWatchButton"
                        text: "Clear"
                        enabled: configurator.provider.watchCount > 0 && !configurator.provider.watchActive
                        onClicked: configurator.provider.clearWatch()
                    }
                }
                ListView {
                    id: watchList
                    objectName: "watchlistView"
                    visible: watchPanelExpanded.checked
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: configurator.provider.watchlist
                    clip: true
                    delegate: Item {
                        id: watchRow
                        objectName: "watchlistRow"
                        required property int index
                        required property string name
                        required property string status
                        required property string text
                        width: watchList.width
                        height: 34
                        RowLayout {
                            anchors.fill: parent
                            Label { textFormat: Text.PlainText; text: watchRow.name; Layout.preferredWidth: Math.min(260, watchList.width / 3); elide: Text.ElideMiddle }
                            Label { textFormat: Text.PlainText; text: watchRow.text; font.family: "monospace"; color: "#83e1ce"; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { textFormat: Text.PlainText; text: watchRow.status; color: "#99abbf" }
                            Button {
                                objectName: "removeWatchButton"
                                property int watchRowIndex: watchRow.index
                                text: "Remove"
                                enabled: !configurator.provider.watchActive
                                onClicked: configurator.provider.removeWatch(watchRow.index)
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    Label { textFormat: Text.PlainText; anchors.centerIn: parent; visible: configurator.provider.watchCount === 0; text: "Select a scalar Field and add it to the watchlist."; color: "#718297" }
                }
            }
        }
        Label {
            textFormat: Text.PlainText
            objectName: "statusLabel"
            text: configurator.provider.statusText
            color: configurator.provider.connected ? "#83e1ce" : "#99abbf"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Loader { Layout.fillWidth: true; active: configurator.footerComponent !== null; sourceComponent: configurator.footerComponent }
    }
    Connections {
        target: configurator.provider.endpoints
        function onModelReset() {
            Qt.callLater(function() { endpointTree.expandRecursively(0, 2) })
        }
    }
}
