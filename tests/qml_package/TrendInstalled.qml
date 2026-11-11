import QtQuick
import QtQuick.Window
import Telemetry.UX
Window {
    width: 640; height: 400; visible: true
    TrendPanel { anchors.fill: parent; provider: testProvider; history: testHistory }
}
