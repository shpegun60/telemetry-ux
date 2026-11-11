import QtQuick
import QtQuick.Controls
import Telemetry.UX

Flickable {
    id: scroll
    objectName: "trendDockScroll"
    clip: true
    contentWidth: width
    contentHeight: Math.max(height, 300)
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    TrendPanel {
        width: scroll.width
        height: scroll.contentHeight
        provider: device
        history: trendHistory
    }
}
