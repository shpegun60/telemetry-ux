// The Log's lines (LG-1) as a host's list shows them: a LogModel given as
// logLinesModel, each row a LogLine in an ItemDelegate, the current row
// expanded - as the client's Log panel does. MIT.
import QtQuick
import QtQuick.Controls
import Telemetry.UX

ApplicationWindow {
    id: window
    width: 900
    height: 560
    visible: true
    color: Theme.surfaceWindow
    font.pointSize: Type.body

    ListView {
        id: list
        objectName: "logLinesView"
        anchors.fill: parent
        model: logLinesModel
        currentIndex: -1
        activeFocusOnTab: true
        clip: true
        readonly property bool narrow: width < 360
        delegate: ItemDelegate {
            id: entry
            objectName: "logRow"
            required property int index
            required property var model
            readonly property bool isCurrent: ListView.isCurrentItem
            width: ListView.view.width
            implicitHeight: isCurrent ? Math.max(Type.rowHeight, line.implicitHeight + topPadding + bottomPadding)
                                      : Type.rowHeight
            highlighted: isCurrent
            leftPadding: Type.padding
            rightPadding: Type.padding
            topPadding: 2
            bottomPadding: 2
            background: Rectangle {
                color: entry.highlighted ? (list.activeFocus ? Theme.stateSelected : Theme.stateSelectedInactive)
                     : entry.hovered ? Theme.stateHover : "transparent"
            }
            Accessible.name: line.spoken
            contentItem: LogLine {
                id: line
                timeText: entry.model.timeText
                severity: entry.model.severity
                source: entry.model.source
                kind: entry.model.kind
                path: entry.model.path
                values: entry.model.values
                words: entry.model.text
                detail: entry.model.detail
                expanded: entry.isCurrent
                narrow: list.narrow
                highlighted: entry.highlighted
                focused: list.activeFocus
            }
            onClicked: { list.currentIndex = index; list.forceActiveFocus() }
        }
    }
}
