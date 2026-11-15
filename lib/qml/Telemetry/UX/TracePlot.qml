// The live Plot (LP-1): the values the Watchlist reads, as lines over the
// time of day, following the newest reading. A value is put on it from the
// Watchlist row's menu (Plot) and taken off by its chip's ×; its readings
// stay in memory either way - the last ten minutes of every watched value.
// Follow keeps the newest readings at the right edge; unticked, the picture
// holds still while reading goes on. Presentation only: WatchTraces keeps the
// readings and TraceView draws them. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: plot
    // A provider's WatchTraces (provider.watchTraces); null shows nothing.
    property var traces: null
    // The curves' colours, by palette place (WatchTraces.colorIndex).
    property var curveColors: ["#58a6ff", "#f3b562", "#56c8a0", "#d49bff", "#ff7e8f", "#62d4e6", "#d8ce67", "#a3b8cc",
                           "#9cc3ff", "#e98b55", "#a5d98c", "#ed9fce", "#b6a0ef", "#7ac7c4", "#cfaa85", "#c7cdd5"]
    property alias following: view.following
    property alias spanSeconds: view.spanSeconds
    readonly property var plotted: traces ? traces.plotted : []
    readonly property alias view: view
    implicitWidth: 480
    implicitHeight: 280

    // A reading as the chip shows it: a whole number whole, else up to six
    // significant digits; nothing read yet, a dash.
    function valueText(value) {
        if (value === undefined || value === null || isNaN(value))
            return "–"
        if (Number.isInteger(value))
            return String(value)
        return String(Number(value.toPrecision(6)))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Type.spacing

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            Layout.topMargin: Type.tight
            spacing: Type.groupSpacing
            CheckBox {
                id: follow
                objectName: "plotFollow"
                text: qsTr("Follow")
                checked: view.following
                focusPolicy: Qt.TabFocus
                onToggled: view.following = checked
                Accessible.name: qsTr("Follow the newest readings")
                Accessible.description: qsTr("Keep the newest readings in view. Unticked, the picture holds still; reading goes on.")
            }
            Label {
                objectName: "plotSpan"
                color: Theme.textSecondary
                font.pointSize: Type.caption
                text: qsTr("Last minute")
            }
            Item { Layout.fillWidth: true }
        }

        // A chip per plotted value: its colour, its path, its newest reading,
        // and × to take it off the Plot (its readings are kept).
        Flow {
            id: legend
            objectName: "plotLegend"
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.spacing
            visible: plot.plotted.length > 0
            Repeater {
                model: plot.plotted
                delegate: Rectangle {
                    id: chip
                    required property string modelData
                    required property int index
                    objectName: "plotChip"
                    readonly property string path: modelData
                    readonly property color curve: {
                        const at = plot.traces ? plot.traces.colorIndex(path) : -1
                        return at >= 0 && at < plot.curveColors.length ? plot.curveColors[at] : Theme.textSecondary
                    }
                    readonly property string shown: {
                        plot.traces ? plot.traces.revision : 0
                        return plot.valueText(plot.traces ? plot.traces.latest(path) : undefined)
                    }
                    implicitWidth: chipRow.implicitWidth + 2 * Type.padding
                    implicitHeight: Type.controlHeight
                    radius: Type.radius
                    color: Theme.surface
                    border.color: Theme.outlineVariant
                    RowLayout {
                        id: chipRow
                        anchors.fill: parent
                        anchors.leftMargin: Type.padding
                        anchors.rightMargin: Type.tight
                        spacing: Type.spacing
                        Rectangle {
                            Layout.preferredWidth: Type.iconSize
                            Layout.preferredHeight: 2
                            color: chip.curve
                            Accessible.ignored: true
                        }
                        Text {
                            id: chipName
                            objectName: "plotChipName"
                            Layout.maximumWidth: Type.bodyPixels * 18
                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            color: Theme.textPrimary
                            font.pointSize: Type.body
                            text: chip.path
                            HoverHandler { id: nameHover }
                            PlainToolTip { visible: nameHover.hovered && chipName.truncated; text: chip.path; delay: 700 }
                        }
                        Text {
                            objectName: "plotChipValue"
                            Layout.minimumWidth: valueWidth.advanceWidth
                            horizontalAlignment: Text.AlignRight
                            textFormat: Text.PlainText
                            color: Theme.textPrimary
                            font.family: Type.fixedFamily
                            font.pointSize: Type.body
                            text: chip.shown
                            TextMetrics {
                                id: valueWidth
                                font.family: Type.fixedFamily
                                font.pointSize: Type.body
                                text: "-0.000000"
                            }
                        }
                        ToolButton {
                            objectName: "plotChipRemove"
                            Layout.preferredWidth: Type.chipHeight
                            Layout.preferredHeight: Type.chipHeight
                            flat: true
                            padding: 0
                            focusPolicy: Qt.TabFocus
                            text: qsTr("×")
                            Accessible.name: qsTr("Remove %1 from the Plot").arg(chip.path)
                            Accessible.description: qsTr("Its readings are kept.")
                            ToolTip.visible: hovered || visualFocus
                            ToolTip.text: qsTr("Remove from the Plot; its readings are kept")
                            onClicked: plot.traces.unplot(chip.path)
                        }
                    }
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("%1, %2").arg(chip.path).arg(chip.shown)
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TraceView {
                id: view
                objectName: "plotView"
                anchors.fill: parent
                traces: plot.traces
                colors: plot.curveColors
                gridColor: Theme.outlineVariant
                labelColor: Theme.textSecondary
                font.family: Type.fixedFamily
                font.pointSize: Type.caption
                visible: plot.plotted.length > 0
                Accessible.role: Accessible.Graphic
                Accessible.name: plot.plotted.length === 1 ? qsTr("Plot of 1 value, %1").arg(view.following ? qsTr("following") : qsTr("held"))
                                                           : qsTr("Plot of %1 values, %2").arg(plot.plotted.length)
                                                                 .arg(view.following ? qsTr("following") : qsTr("held"))
            }
            Label {
                objectName: "plotTimeTitle"
                visible: view.visible
                anchors.right: parent.right
                anchors.rightMargin: Type.padding
                anchors.bottom: parent.bottom
                color: Theme.textMuted
                font.pointSize: Type.caption
                text: qsTr("Time of day")
                HoverHandler { id: titleHover }
                PlainToolTip { visible: titleHover.hovered; text: qsTr("Time this PC received each reading (local time)"); delay: 700 }
            }
            // Nothing plotted: how to plot something.
            ColumnLayout {
                objectName: "plotEmpty"
                visible: plot.plotted.length === 0
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Type.padding, 360)
                spacing: Type.spacing
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    text: plot.traces ? qsTr("Add a value from the Watchlist to plot it.")
                                      : qsTr("Connect to a device to plot its values.")
                }
                Label {
                    Layout.fillWidth: true
                    visible: plot.traces !== null
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pointSize: Type.caption
                    text: qsTr("Right-click a row and choose Plot.")
                }
            }
        }
    }
}
