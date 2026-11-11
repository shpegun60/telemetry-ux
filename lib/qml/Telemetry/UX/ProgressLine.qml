// One progress line, the same everywhere it appears. MIT.
// Determinate when progress is 0..1, indeterminate when it is negative.
// It appears only once its work has lasted 400 ms, and once shown it stays at
// least 500 ms, so fast work never flashes a bar. Colour is the info ramp,
// never the accent, which means "you can press this".
import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ProgressBar {
    id: line

    // 0..1, or negative while the total is not known.
    property real progress: -1
    // Whether the work is still going; the line hides when it stops.
    property bool running: true
    property int thickness: 4
    // A still segment instead of the moving sweep.
    property bool reducedMotion: false
    // What is happening, for screen readers: "Downloading events.log".
    property string description: ""
    property string valueText: ""

    property bool shown: false
    property double shownSince: 0

    from: 0
    to: 1
    value: Math.max(0, Math.min(1, progress))
    indeterminate: progress < 0
    visible: shown
    implicitHeight: thickness
    padding: 0

    Accessible.name: description
    Accessible.description: indeterminate ? qsTr("in progress") : valueText

    Timer {
        id: appear
        interval: 400
        running: line.running && !line.shown
        onTriggered: {
            line.shown = true
            line.shownSince = Date.now()
        }
    }
    Timer {
        id: hold
        onTriggered: line.shown = false
    }
    onRunningChanged: {
        if (running) {
            hold.stop()
        } else if (shown) {
            // Whatever is left of the 500 ms it must stay once shown.
            hold.interval = Math.max(0, 500 - (Date.now() - shownSince))
            hold.restart()
        }
    }

    background: Rectangle {
        implicitHeight: line.thickness
        radius: height / 2
        color: Theme.infoContainer
    }

    contentItem: Item {
        implicitHeight: line.thickness
        // The sweep travels beyond both ends; clip it to the track.
        clip: line.indeterminate

        Rectangle {
            visible: !line.indeterminate
            width: line.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: Theme.info
        }
        Rectangle {
            id: sweep
            visible: line.indeterminate
            width: parent.width * 0.3
            height: parent.height
            radius: height / 2
            color: Theme.info
            x: line.reducedMotion ? (parent.width - width) / 2 : -width

            XAnimator on x {
                from: -sweep.width
                to: sweep.parent ? sweep.parent.width : 0
                duration: 1200
                loops: Animation.Infinite
                running: line.indeterminate && line.visible && !line.reducedMotion
            }
        }
    }
}
