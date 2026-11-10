// Reusable presentation: the caller owns the provider and all I/O. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

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
    property bool expanded: false
    property real headerImplicitHeight: 0
    implicitHeight: headerImplicitHeight + (expanded ? 120 : 0) + topPadding + bottomPadding
    background: Rectangle { color: "#17202b"; radius: 10 }

    // A dock can be much narrower or shorter than the Configurator. Wrap the
    // control groups and scroll vertically instead of clipping actions. The
    // view remains purely presentational: visibility never changes polling.
    contentItem: ScrollView {
        id: watchScroll
        objectName: "watchlistScroll"
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: ScrollBar {
            objectName: "watchlistScrollBar"
            policy: ScrollBar.AsNeeded
            active: true
            parent: watchScroll
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }
        contentItem: ListView {
            id: watchList
            objectName: "watchlistView"
            clip: true
            model: panel.expanded ? panel.provider.watchlist : null
            header: ColumnLayout {
                id: watchLayout
                width: watchList.width
                onImplicitHeightChanged: panel.headerImplicitHeight = implicitHeight
                Component.onCompleted: panel.headerImplicitHeight = implicitHeight
                spacing: 6
                Flow {
                    id: actions
                    objectName: "watchActions"
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    spacing: 6
                    CheckBox {
                        id: watchPanelExpanded
                        objectName: "watchlistExpanded"
                        text: "Watchlist " + panel.provider.watchCount
                        checked: false
                        onCheckedChanged: panel.expanded = checked
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
                Flow {
                    objectName: "watchSettings"
                    visible: panel.expanded
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    spacing: 6
                    ComboBox {
                        objectName: "watchMode"
                        width: Math.min(210, watchLayout.width)
                        readonly property bool readManySupported: panel.provider.watchReadManySupported !== false
                        readonly property bool explicitIndividual: panel.provider.watchRequiresExplicitIndividual === true
                        model: readManySupported ? ["ReadMany", "Individual FieldRead"]
                            : explicitIndividual ? ["ReadMany (unavailable)", "Individual FieldRead"] : ["Individual FieldRead"]
                        currentIndex: (readManySupported || explicitIndividual) && panel.provider.watchIndividual ? 1 : 0
                        enabled: !panel.provider.watchActive && (readManySupported || explicitIndividual)
                        onActivated: panel.provider.watchIndividual = (!readManySupported && !explicitIndividual) || currentIndex === 1
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Interval (ms)"
                        color: "#99abbf"
                        height: watchFrequency.height
                        verticalAlignment: Text.AlignVCenter
                    }
                    SpinBox {
                        id: watchFrequency
                        objectName: "watchFrequency"
                        from: 1
                        to: 60000
                        stepSize: 1
                        editable: true
                        value: panel.provider.watchIntervalMs
                        enabled: !panel.provider.watchActive
                        onValueModified: panel.provider.watchIntervalMs = value
                    }
                    Button {
                        objectName: "clearWatchButton"
                        text: "Clear"
                        enabled: panel.provider.watchCount > 0 && !panel.provider.watchActive
                        onClicked: panel.provider.clearWatch()
                    }
                }
                Label {
                    objectName: "watchIntervalHint"
                    visible: panel.expanded
                    textFormat: Text.PlainText
                    text: "Nominal interval. Fields are read sequentially."
                    color: "#99abbf"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
            delegate: Item {
                id: watchRow
                objectName: "watchlistRow"
                required property int index
                required property string name
                required property string status
                required property string text
                width: watchList.width
                height: rowLayout.implicitHeight + 8
                readonly property bool compact: width < 500
                ColumnLayout {
                    id: rowLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Label {
                        visible: watchRow.compact
                        textFormat: Text.PlainText
                        text: watchRow.name
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            visible: !watchRow.compact
                            textFormat: Text.PlainText
                            text: watchRow.name
                            Layout.preferredWidth: Math.min(260, watchList.width / 3)
                            elide: Text.ElideMiddle
                        }
                        Label {
                            objectName: "watchValueText"
                            textFormat: Text.PlainText
                            text: watchRow.text
                            font.family: "monospace"
                            color: "#83e1ce"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 1
                            wrapMode: Text.WrapAnywhere
                            Layout.fillHeight: true
                            Layout.minimumHeight: paintedHeight
                        }
                        Label {
                            objectName: "watchRowStatus"
                            textFormat: Text.PlainText
                            text: watchRow.status
                            color: "#99abbf"
                            Layout.minimumWidth: 1
                            Layout.preferredWidth: watchRow.compact ? Math.min(100, watchList.width / 4) : implicitWidth
                            wrapMode: Text.WrapAnywhere
                            Layout.fillHeight: true
                            Layout.minimumHeight: paintedHeight
                        }
                        Button {
                            objectName: "removeWatchButton"
                            property int watchRowIndex: watchRow.index
                            text: "Remove"
                            enabled: !panel.provider.watchActive
                            onClicked: panel.provider.removeWatch(watchRow.index)
                        }
                    }
                }
            }
            footer: Label {
                textFormat: Text.PlainText
                width: watchList.width
                height: visible ? 80 : 0
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                visible: panel.expanded && panel.provider.watchCount === 0
                text: "Select a scalar Field and add it to the watchlist."
                color: "#718297"
            }
        }
    }
    Connections {
        target: panel.Window.window
        function onActiveFocusItemChanged() {
            const focused = panel.Window.window.activeFocusItem
            let ancestor = focused
            while (ancestor && ancestor !== watchList.contentItem)
                ancestor = ancestor.parent
            if (!ancestor)
                return
            const viewport = watchList
            const position = focused.mapToItem(watchList.contentItem, 0, 0)
            let offset = viewport.contentY
            if (position.y < offset)
                offset = position.y
            else if (position.y + focused.height > offset + viewport.height)
                offset = position.y + focused.height - viewport.height
            const minimum = viewport.originY
            const maximum = Math.max(minimum, minimum + viewport.contentHeight - viewport.height)
            viewport.contentY = Math.max(minimum, Math.min(offset, maximum))
        }
    }
}
