// One Field from the existing Watchlist; all reading remains owned by the caller.
// One row - the Field and Clear - and the plot under it; the state is one line
// in the plot, centred while there is nothing to draw, a caption above the
// line once there is. Every size follows the type scale. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Telemetry.UX

Pane {
    id: panel
    required property var provider
    required property var history
    objectName: "trendPanel"
    implicitWidth: 440
    implicitHeight: 200
    padding: Type.padding
    Binding { target: panel.history; property: "source"; value: panel.provider ? panel.provider.watchlist : null }
    Connections { target: panel.history; function onPointsChanged() { plot.requestPaint() } }
    ColumnLayout {
        anchors.fill: parent
        spacing: Type.spacing
        RowLayout {
            Layout.fillWidth: true
            spacing: Type.spacing
            ComboBox {
                id: choice
                objectName: "trendField"
                Layout.fillWidth: true
                Layout.maximumWidth: Type.controlHeight * 15
                model: panel.provider ? panel.provider.watchlist : null
                textRole: "name"
                currentIndex: -1
                displayText: currentIndex < 0 ? qsTr("Choose a watched Field") : currentText
                onActivated: panel.history.row = currentIndex
                Connections {
                    target: panel.history
                    function onRowChanged() { choice.currentIndex = panel.history.row }
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "clearTrend"
                flat: true
                text: qsTr("Clear")
                enabled: panel.history.points.length > 0
                onClicked: panel.history.clear()
            }
        }
        Canvas {
            id: plot
            objectName: "trendPlot"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Type.rowHeight * 5
            readonly property bool empty: panel.history.points.length < 1
            readonly property real labelPixels: Math.round(Type.bodyPixels / Type.ratio)
            readonly property color surface: Theme.surfaceWindow
            readonly property color axis: Theme.outline
            readonly property color label: Theme.textSecondary
            readonly property color line: Theme.accent
            Accessible.name: qsTr("Trend chart, this computer's time")
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onLabelPixelsChanged: requestPaint()
            HoverHandler { id: plotHover }
            ToolTip.visible: plotHover.hovered && !plot.empty
            ToolTip.delay: 700
            ToolTip.text: qsTr("Time is this computer's, not the device's. The last 512 readings.")
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = surface
                ctx.fillRect(0, 0, width, height)
                const p = panel.history.points
                if (p.length < 1) return
                let ymin = p[0].value, ymax = ymin
                const xmin = p[0].ms, xmax = p[p.length - 1].ms
                for (let i = 1; i < p.length; ++i) {
                    ymin = Math.min(ymin, p[i].value)
                    ymax = Math.max(ymax, p[i].value)
                }
                // Normalize first so opposite-sign finite F64 extrema never
                // overflow the span to Infinity.
                const scale = Math.max(Math.abs(ymin), Math.abs(ymax), Number.MIN_VALUE)
                const minY = ymin / scale, rangeY = ymax / scale - minY
                const room = Math.round(labelPixels * 1.6)
                ctx.font = labelPixels + "px sans-serif"
                const left = Math.ceil(Math.max(ctx.measureText(Number(ymax).toPrecision(4)).width,
                                                ctx.measureText(Number(ymin).toPrecision(4)).width)) + 8
                const top = room, right = Math.max(left + 1, width - 8)
                const bottom = Math.max(top + 1, height - room)
                ctx.strokeStyle = axis; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(left, top); ctx.lineTo(left, bottom); ctx.lineTo(right, bottom); ctx.stroke()
                ctx.fillStyle = label
                ctx.fillText(Number(ymax).toPrecision(4), 2, top + labelPixels * 0.7)
                ctx.fillText(Number(ymin).toPrecision(4), 2, bottom)
                ctx.fillText(String(xmin) + " ms", left, height - 4)
                const endLabel = String(xmax) + " ms"
                ctx.fillText(endLabel, Math.max(left + 65, right - ctx.measureText(endLabel).width), height - 4)
                ctx.strokeStyle = line; ctx.lineWidth = 2; ctx.beginPath()
                for (let i = 0; i < p.length; ++i) {
                    const x = left + (p[i].ms - xmin) / Math.max(1, xmax - xmin) * (right - left)
                    const normalized = rangeY === 0 ? 0.5 : (p[i].value / scale - minY) / rangeY
                    const y = bottom - normalized * (bottom - top)
                    if (i === 0 || p[i].breakBefore) ctx.moveTo(x, y)
                    else ctx.lineTo(x, y)
                    // Single samples are visible too.
                    ctx.moveTo(x + 2, y); ctx.arc(x, y, 2, 0, 2 * Math.PI); ctx.moveTo(x, y)
                }
                ctx.stroke()
            }
            Label {
                objectName: "trendState"
                x: plot.empty ? Math.round((plot.width - width) / 2) : plot.labelPixels * 4
                y: plot.empty ? Math.round((plot.height - height) / 2) : 0
                width: plot.empty ? Math.min(implicitWidth, plot.width - 2 * Type.padding)
                                  : plot.width - plot.labelPixels * 4
                horizontalAlignment: plot.empty ? Text.AlignHCenter : Text.AlignLeft
                wrapMode: plot.empty ? Text.Wrap : Text.NoWrap
                elide: Text.ElideRight
                textFormat: Text.PlainText
                font.pointSize: plot.empty ? Type.body : Type.caption
                color: Theme.textSecondary
                text: panel.history.state
            }
        }
    }
}
