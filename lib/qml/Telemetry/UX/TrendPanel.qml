// One Field from the existing Watchlist; all reading remains owned by the caller. MIT.
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
    implicitHeight: 300
    Binding { target: panel.history; property: "source"; value: panel.provider ? panel.provider.watchlist : null }
    Connections { target: panel.history; function onPointsChanged() { plot.requestPaint() } }
    ColumnLayout {
        anchors.fill: parent
        Label { text: "TREND"; font.bold: true }
        ComboBox {
            id: choice
            objectName: "trendField"
            Layout.fillWidth: true
            model: panel.provider ? panel.provider.watchlist : null
            textRole: "name"
            currentIndex: -1
            displayText: currentIndex < 0 ? "Select a watched Field" : currentText
            onActivated: panel.history.row = currentIndex
            Connections {
                target: panel.history
                function onRowChanged() { choice.currentIndex = panel.history.row }
            }
        }
        Label {
            objectName: "trendState"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: panel.history.state
        }
        Canvas {
            id: plot
            objectName: "trendPlot"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            Accessible.name: "Trend chart, host presentation time"
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = "#11151c"
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
                const left = 54, top = 12, right = Math.max(left + 1, width - 12)
                const bottom = Math.max(top + 1, height - 30)
                ctx.strokeStyle = "#8193a6"; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(left, top); ctx.lineTo(left, bottom); ctx.lineTo(right, bottom); ctx.stroke()
                ctx.fillStyle = "#c6d4e3"; ctx.font = "11px sans-serif"
                ctx.fillText(Number(ymax).toPrecision(4), 2, top + 8)
                ctx.fillText(Number(ymin).toPrecision(4), 2, bottom)
                ctx.fillText(String(xmin) + " ms", left, height - 8)
                const endLabel = String(xmax) + " ms"
                ctx.fillText(endLabel, Math.max(left + 65, right - ctx.measureText(endLabel).width), height - 8)
                ctx.strokeStyle = "#64d6ca"; ctx.lineWidth = 2; ctx.beginPath()
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
        }
        RowLayout {
            Label { Layout.fillWidth: true; text: "Host presentation time, not device time · up to 512 readings"; wrapMode: Text.Wrap }
            Button { objectName: "clearTrend"; text: "Clear"; onClicked: panel.history.clear() }
        }
    }
}
