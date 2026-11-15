// The live Plot (LP-1): the values the Watchlist reads, as lines over the
// time of day, following the newest reading. A value is put on it from the
// Watchlist row's menu (Plot) and taken off by its chip's ×; its readings
// stay in memory either way - the last ten minutes of every watched value.
// Follow keeps the newest readings at the right edge; unticked, the picture
// holds still while reading goes on. 10 s, 1 min, 10 min or All choose the
// span and follow again; a drag, the wheel, the arrows, + and - move it and
// stop following, and the chip in the corner says how far behind it is and
// follows again (End). The cursor follows the pointer, snapped to a reading;
// its time shows under the axis and each chip shows its value there. A click
// or Enter pins a reference: the cursor then says the time since it, each
// chip its value's difference; Esc takes it away. Presentation only:
// WatchTraces keeps the readings and TraceView draws them. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
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
    // The chips read their values four times a second, however fast the
    // readings come.
    property int shownTick: 0
    Timer {
        interval: 250
        repeat: true
        running: plot.visible && plot.plotted.length > 0
        onTriggered: plot.shownTick++
    }
    // Plotted, but nothing read yet for any of them.
    readonly property bool waiting: {
        plot.shownTick
        if (!traces || plotted.length === 0)
            return false
        for (const path of plotted)
            if (traces.count(path) > 0)
                return false
        return true
    }
    // "UTC+01:00": this PC's offset, for the time axis' tooltip.
    function utcOffset() {
        const minutes = -new Date().getTimezoneOffset()
        const sign = minutes < 0 ? "−" : "+"
        const whole = Math.abs(minutes)
        return "UTC" + sign + String(Math.floor(whole / 60)).padStart(2, "0") + ":" + String(whole % 60).padStart(2, "0")
    }
    implicitWidth: 480
    implicitHeight: 280

    // A reading as the chip shows it: a whole number whole, else up to six
    // significant digits; nothing read yet, a dash.
    // "12 s", "1:12", "1:02:03": how far behind the newest reading.
    function behindText(seconds) {
        const whole = Math.round(seconds)
        if (whole < 60)
            return qsTr("%1 s").arg(whole)
        const h = Math.floor(whole / 3600), m = Math.floor(whole % 3600 / 60), sec = whole % 60
        const two = n => String(n).padStart(2, "0")
        return h > 0 ? "%1:%2:%3".arg(h).arg(two(m)).arg(two(sec)) : "%1:%2".arg(m).arg(two(sec))
    }
    // The same, in words for a screen reader.
    function behindWords(seconds) {
        const whole = Math.round(seconds)
        const m = Math.floor(whole / 60), sec = whole % 60
        if (m === 0)
            return qsTr("%Ln seconds", "", sec)
        return sec === 0 ? qsTr("%Ln minutes", "", m)
                         : qsTr("%1 %2").arg(qsTr("%Ln minutes", "", m)).arg(qsTr("%Ln seconds", "", sec))
    }
    // The cursor is in the picture: the chips show their values there. Once
    // its reading scrolls out they go back to the newest.
    readonly property bool cursorInView: view.cursorShown && view.cursorX >= view.plotArea.left
                                         && view.cursorX <= view.plotArea.right
    // "12:04:31.250": a time on this PC's clock, always to the millisecond.
    function clockText(ms) {
        return Qt.formatTime(new Date(ms), "HH:mm:ss.zzz")
    }
    // "1.250 s", "1:12.250": the time between the reference and the cursor.
    function spanText(ms) {
        const sign = ms < 0 ? "−" : ""
        const whole = Math.abs(ms) / 1000
        if (whole < 60)
            return qsTr("%1 s").arg(sign + whole.toFixed(3))
        const m = Math.floor(whole / 60)
        return sign + "%1:%2".arg(m).arg((whole - 60 * m).toFixed(3).padStart(6, "0"))
    }
    // "12.4 s", "1:12": how long before the newest reading the cursor is.
    function agoText(seconds) {
        return seconds < 60 ? qsTr("%1 s").arg(seconds.toFixed(1)) : behindText(seconds)
    }
    // "+0.12", "−3": a difference, signed.
    function deltaText(value) {
        if (value === undefined || value === null || isNaN(value))
            return "–"
        const text = valueText(Math.abs(value))
        return (value < 0 ? "−" : "+") + text
    }
    function valueText(value) {
        if (value === undefined || value === null || isNaN(value))
            return "–"
        // One minus sign everywhere, the typographic one.
        const text = Number.isInteger(value) ? String(value) : String(Number(value.toPrecision(6)))
        return text.startsWith("-") ? "−" + text.substring(1) : text
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
                ToolTip.visible: hovered
                ToolTip.delay: 700
                ToolTip.text: qsTr("Keep the newest readings in view; unticked, the picture holds still")
                Accessible.name: qsTr("Follow the newest readings")
                Accessible.description: qsTr("Keep the newest readings in view. Unticked, the picture holds still; reading goes on.")
            }
            // 10 s | 1 min | 10 min | All, as the Watchlist's List | Tree:
            // the chosen one filled, Left and Right move between them.
            Row {
                id: presets
                objectName: "plotPresets"
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("Time span")
                Layout.preferredHeight: Type.controlHeight
                spacing: 0
                ButtonGroup { id: presetGroup }
                Repeater {
                    id: presetButtons
                    model: [
                        { seconds: 10, text: qsTr("10 s"), name: qsTr("Last 10 seconds") },
                        { seconds: 60, text: qsTr("1 min"), name: qsTr("Last minute") },
                        { seconds: 600, text: qsTr("10 min"), name: qsTr("Last 10 minutes") },
                        { seconds: 0, text: qsTr("All"), name: qsTr("Everything kept") }
                    ]
                    delegate: Button {
                        required property var modelData
                        required property int index
                        objectName: "plotPreset_" + modelData.seconds
                        width: implicitContentWidth + leftPadding + rightPadding
                        height: Type.controlHeight
                        verticalPadding: 0
                        font.pointSize: Type.caption
                        text: modelData.text
                        checkable: true
                        checked: view.preset === modelData.seconds
                        flat: !checked
                        ButtonGroup.group: presetGroup
                        focusPolicy: checked || activeFocus ? Qt.StrongFocus : Qt.ClickFocus
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.name
                        Accessible.description: qsTr("Key %1").arg(index + 1)
                        onClicked: view.choosePreset(modelData.seconds)
                        Keys.onLeftPressed: {
                            const at = Math.max(0, index - 1)
                            view.choosePreset(presetButtons.model[at].seconds)
                            presetButtons.itemAt(at).forceActiveFocus(Qt.TabFocusReason)
                        }
                        Keys.onRightPressed: {
                            const at = Math.min(presetButtons.count - 1, index + 1)
                            view.choosePreset(presetButtons.model[at].seconds)
                            presetButtons.itemAt(at).forceActiveFocus(Qt.TabFocusReason)
                        }
                        ToolTip.visible: hovered || visualFocus
                        ToolTip.delay: 700
                        ToolTip.text: qsTr("%1 (%2)").arg(modelData.name).arg(index + 1)
                    }
                }
            }
            IconToolButton {
                objectName: "plotZoomOut"
                iconName: "zoomOut"
                text: qsTr("Zoom out")
                shortcutText: qsTr("Minus")
                onClicked: view.zoom(1.25)
            }
            IconToolButton {
                objectName: "plotZoomIn"
                iconName: "zoomIn"
                text: qsTr("Zoom in")
                shortcutText: qsTr("Plus")
                onClicked: view.zoom(0.8)
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
                    // Its newest reading; its reading at the cursor while the
                    // cursor is shown.
                    readonly property var current: {
                        plot.shownTick
                        view.cursorUtcMs
                        if (!plot.traces)
                            return undefined
                        return plot.cursorInView ? view.valueAt(path) : plot.traces.latest(path)
                    }
                    // A bool or an enum in words ("true", "Idle"), a number as one.
                    readonly property string words: plot.traces && current !== undefined ? plot.traces.stateText(path, current) : ""
                    readonly property string shown: words.length > 0 ? words : plot.valueText(current)
                    // Its difference from the reference, once one is pinned.
                    readonly property string delta: {
                        // A state has no difference: a number does.
                        if (!view.referenceShown || words.length > 0)
                            return ""
                        const reference = view.referenceValue(path)
                        return reference === undefined || current === undefined ? "Δ –"
                                                                                : "Δ " + plot.deltaText(current - reference)
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
                        Text {
                            objectName: "plotChipDelta"
                            visible: chip.delta.length > 0
                            // As wide as a long difference from the moment a
                            // reference is pinned, so the legend never re-wraps.
                            Layout.minimumWidth: deltaWidth.advanceWidth
                            TextMetrics {
                                id: deltaWidth
                                font.family: Type.fixedFamily
                                font.pointSize: Type.caption
                                text: "Δ −0.000000"
                            }
                            textFormat: Text.PlainText
                            color: Theme.textSecondary
                            font.family: Type.fixedFamily
                            font.pointSize: Type.caption
                            text: chip.delta
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
                    Accessible.name: plot.cursorInView ? qsTr("%1, %2 at %3").arg(chip.path).arg(chip.shown).arg(plot.clockText(view.cursorUtcMs))
                                                      : qsTr("%1, %2, latest reading").arg(chip.path).arg(chip.shown)
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
                cursorColor: Theme.textMuted
                // A row of its own under the time labels for the chips.
                bottomInset: Type.chipHeight + Type.tight
                // A bool's or an enum's lane, a row high.
                laneHeight: Type.rowHeight
                laneFillColor: Theme.neutralContainer
                laneTextColor: Theme.textPrimary
                referenceColor: Theme.info
                dotRingColor: Theme.surfaceWindow
                font.family: Type.fixedFamily
                font.pointSize: Type.caption
                visible: plot.plotted.length > 0 && !plot.waiting
                Accessible.role: Accessible.Graphic
                Accessible.name: {
                    const state = view.following ? qsTr("following")
                                                  : qsTr("%1 behind").arg(plot.behindWords(view.behindSeconds))
                    return plot.plotted.length === 1 ? qsTr("Plot of 1 value, %1").arg(state)
                                                     : qsTr("Plot of %1 values, %2").arg(plot.plotted.length).arg(state)
                }
                Accessible.description: qsTr("Arrows pan, plus and minus zoom, Shift+arrows step the cursor, Enter pins a reference, Home goes to the oldest reading, End follows, 1 to 4 choose the span.")
            }
            // The keys are on the graph: the lists' focus frame round it.
            Rectangle {
                objectName: "plotFocus"
                visible: view.visible && view.activeFocus
                x: view.plotArea.x - 1
                y: view.plotArea.y - 1
                width: view.plotArea.width + 2
                height: view.plotArea.height + 2
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
                Accessible.ignored: true
            }
            // The reference's time, under the axis where it is pinned.
            Rectangle {
                objectName: "plotReferenceChip"
                visible: view.visible && view.referenceShown && view.referenceX >= view.plotArea.left
                         && view.referenceX <= view.plotArea.right
                width: referenceText.implicitWidth + 2 * Type.padding
                height: Type.chipHeight
                x: Math.max(0, Math.min(parent.width - width, view.referenceX - width / 2))
                y: view.height - height
                radius: Type.radius
                color: Theme.infoContainer
                border.color: Theme.info
                Text {
                    id: referenceText
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    color: Theme.textPrimary
                    text: plot.clockText(view.referenceUtcMs)
                }
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Reference at %1").arg(referenceText.text)
                Accessible.description: qsTr("Escape removes it.")
            }
            // The cursor's time under the axis: how long ago while following,
            // the time since the reference once one is pinned.
            Rectangle {
                objectName: "plotCursorChip"
                visible: view.visible && view.cursorShown && view.cursorX >= view.plotArea.left
                         && view.cursorX <= view.plotArea.right
                width: cursorText.implicitWidth + 2 * Type.padding
                height: Type.chipHeight
                x: Math.max(0, Math.min(parent.width - width, view.cursorX - width / 2))
                y: view.height - height
                radius: Type.radius
                color: Theme.surface
                border.color: Theme.outline
                Text {
                    id: cursorText
                    objectName: "plotCursorText"
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    color: Theme.textPrimary
                    text: {
                        const at = plot.clockText(view.cursorUtcMs)
                        if (view.referenceShown)
                            return qsTr("Δ %1 · %2").arg(plot.spanText(view.cursorUtcMs - view.referenceUtcMs)).arg(at)
                        if (view.following)
                            return qsTr("%1 · %2 ago").arg(at).arg(plot.agoText(view.cursorAgoSeconds))
                        return at
                    }
                }
                Accessible.role: Accessible.StaticText
                Accessible.name: view.referenceShown
                                 ? qsTr("Cursor at %1, %2 from the reference").arg(plot.clockText(view.cursorUtcMs))
                                       .arg(plot.spanText(view.cursorUtcMs - view.referenceUtcMs))
                                 : view.following ? qsTr("Cursor at %1, %2 seconds ago").arg(plot.clockText(view.cursorUtcMs))
                                                        .arg(view.cursorAgoSeconds.toFixed(1))
                                                  : qsTr("Cursor at %1").arg(plot.clockText(view.cursorUtcMs))
            }
            // Held: how far behind the newest reading, and a way back to it.
            Basic.Button {
                id: behind
                objectName: "plotBehind"
                visible: view.visible && !view.following
                x: view.plotArea.right - width - Type.padding
                y: view.plotArea.top + Type.padding
                height: Type.chipHeight
                padding: Type.tight
                leftPadding: Type.padding
                rightPadding: Type.padding
                focusPolicy: Qt.TabFocus
                font.pointSize: Type.caption
                readonly property bool atNewest: view.behindSeconds < 0.5 || view.plotArea.width < 240
                text: atNewest ? qsTr("Follow (End)")
                               : qsTr("%1 behind · Follow (End)").arg(plot.behindText(view.behindSeconds))
                Accessible.name: atNewest ? qsTr("Follow the newest readings")
                                          : qsTr("%1 behind. Follow the newest readings").arg(plot.behindWords(view.behindSeconds))
                Accessible.description: qsTr("End")
                onClicked: view.follow()
                // A chip over the plot, not a toolbar button: the time in the
                // fixed face, the way back in the primary text colour.
                contentItem: Row {
                    spacing: 0
                    Text {
                        visible: !behind.atNewest
                        text: plot.behindText(view.behindSeconds) + " " + qsTr("behind") + " · "
                        font.family: Type.fixedFamily
                        font.pointSize: Type.caption
                        color: Theme.textSecondary
                        Accessible.ignored: true
                    }
                    Text {
                        text: qsTr("Follow (End)")
                        font.pointSize: Type.caption
                        color: Theme.textPrimary
                        Accessible.ignored: true
                    }
                }
                background: Rectangle {
                    radius: Type.radius
                    color: behind.down ? Theme.statePressed : behind.hovered ? Theme.stateHover : Theme.surface
                    border.width: behind.visualFocus ? 2 : 1
                    border.color: behind.visualFocus ? Theme.focusRing : Theme.outline
                }
            }
            Label {
                objectName: "plotTimeTitle"
                visible: view.visible
                anchors.right: parent.right
                anchors.rightMargin: Type.padding
                anchors.bottom: parent.bottom
                color: Theme.textSecondary
                font.pointSize: Type.caption
                text: qsTr("Time of day")
                HoverHandler { id: titleHover }
                PlainToolTip {
                    visible: titleHover.hovered
                    text: qsTr("Time this PC received each reading (local time, %1)").arg(plot.utcOffset())
                    delay: 700
                }
            }
            // Plotted, nothing read yet.
            Label {
                objectName: "plotWaiting"
                visible: plot.waiting
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Type.padding, 360)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                text: plot.plotted.length === 1 ? qsTr("Waiting for the first reading of %1…").arg(plot.plotted[0])
                                                : qsTr("Waiting for the first readings…")
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
