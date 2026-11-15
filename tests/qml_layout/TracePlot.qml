import QtQuick
import QtQuick.Controls
import Telemetry.UX

ApplicationWindow {
    width: 760
    height: 360
    visible: true
    color: Theme.surfaceWindow
    TracePlot {
        objectName: "tracePlot"
        anchors.fill: parent
        traces: fixtureTraces
    }
}
