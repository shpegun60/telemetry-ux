// Reusable presentation: the caller owns the provider and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: panel
    required property var provider
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
    objectName: "watchlistPane"
    implicitHeight: (watchPanelExpanded.checked ? 220 : 54)
        + (watchStatusLabel.visible ? watchStatusLabel.implicitHeight + 6 : 0)
    background: Rectangle { color: "#17202b"; radius: 10 }
    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            CheckBox {
                id: watchPanelExpanded
                objectName: "watchlistExpanded"
                text: "Watchlist " + panel.provider.watchCount
                checked: false
            }
            Button {
                objectName: "addWatchButton"
                text: "Add selected Field"
                enabled: panel.provider.canAddWatch && !panel.provider.watchActive
                onClicked: {
                    if (panel.provider.addWatch())
                        watchPanelExpanded.checked = true
                }
            }
            Button {
                objectName: "refreshWatchButton"
                text: "Read once"
                enabled: panel.provider.connected && panel.provider.watchCount > 0 && !panel.provider.watchActive
                onClicked: panel.provider.refreshWatch()
            }
            Button {
                objectName: "startWatchButton"
                text: panel.provider.watchActive ? "Stop" : "Start polling"
                enabled: panel.provider.watchActive || (panel.provider.connected && panel.provider.watchCount > 0)
                onClicked: panel.provider.watchActive ? panel.provider.stopWatch() : panel.provider.startWatch()
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            id: watchStatusLabel
            objectName: "watchStatusLabel"
            textFormat: Text.PlainText
            text: panel.provider.watchPending ? "Reading…" : panel.provider.watchStatus
            color: "#99abbf"
            visible: text.length > 0
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            visible: watchPanelExpanded.checked
            Layout.fillWidth: true
            ComboBox {
                objectName: "watchMode"
                Layout.preferredWidth: 210
                readonly property bool readManySupported: panel.provider.watchReadManySupported !== false
                readonly property bool explicitIndividual: panel.provider.watchRequiresExplicitIndividual === true
                model: readManySupported ? ["ReadMany", "Individual FieldRead"]
                    : explicitIndividual ? ["ReadMany (unavailable)", "Individual FieldRead"] : ["Individual FieldRead"]
                currentIndex: (readManySupported || explicitIndividual) && panel.provider.watchIndividual ? 1 : 0
                enabled: !panel.provider.watchActive && (readManySupported || explicitIndividual)
                onActivated: panel.provider.watchIndividual = (!readManySupported && !explicitIndividual) || currentIndex === 1
            }
            Label { textFormat: Text.PlainText; text: "Interval (ms)"; color: "#99abbf" }
            SpinBox {
                objectName: "watchFrequency"
                from: 1
                to: 60000
                stepSize: 1
                editable: true
                value: panel.provider.watchIntervalMs
                enabled: !panel.provider.watchActive
                onValueModified: panel.provider.watchIntervalMs = value
            }
            Label { textFormat: Text.PlainText; text: "Nominal interval · sequential scalar reads"; color: "#99abbf"; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
            Button {
                objectName: "clearWatchButton"
                text: "Clear"
                enabled: panel.provider.watchCount > 0 && !panel.provider.watchActive
                onClicked: panel.provider.clearWatch()
            }
        }
        ListView {
            id: watchList
            objectName: "watchlistView"
            visible: watchPanelExpanded.checked
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: panel.provider.watchlist
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
                        enabled: !panel.provider.watchActive
                        onClicked: panel.provider.removeWatch(watchRow.index)
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {}
            Label { textFormat: Text.PlainText; anchors.centerIn: parent; visible: panel.provider.watchCount === 0; text: "Select a scalar Field and add it to the watchlist."; color: "#718297" }
        }
    }
}
