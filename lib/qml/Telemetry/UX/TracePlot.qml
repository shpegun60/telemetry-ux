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
// chip its value's difference; Esc takes it away. The values fit what is in
// view, or the curve whose name was clicked; Ctrl and the wheel, the wheel
// over the value labels, a drag there or Up and Down move them by hand, and
// Auto (A) fits them again. A chip's colour mark opens its settings - its
// colour, and the multiplier and offset it is drawn with (Trend's words) - and
// its menu (right-click) focuses it, shows its points or removes it; the
// graph's menu shows every curve's points. Presentation only:
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
    // "×100 +0.5": how a curve is drawn when not as read; empty when it is.
    function transformText(k, b) {
        if (k === 1 && b === 0)
            return ""
        const parts = []
        if (k !== 1)
            parts.push("×" + String(k).replace("-", "−"))
        if (b !== 0)
            parts.push((b < 0 ? "−" : "+") + String(Math.abs(b)))
        return parts.join(" ")
    }
    // "12:04:31.250": a time on this PC's clock, always to the millisecond.
    function clockText(ms) {
        return Qt.formatTime(new Date(ms), "HH:mm:ss.zzz")
    }
    // "17.911 s", "1:12.250", "1:02:03.500": the time since reading started.
    function sinceText(ms) {
        const sign = ms < 0 ? "−" : ""
        const whole = Math.abs(ms) / 1000
        if (whole < 60)
            return qsTr("%1 s").arg(sign + whole.toFixed(3))
        const h = Math.floor(whole / 3600)
        const m = Math.floor(whole / 60) % 60
        const s = (whole % 60).toFixed(3).padStart(6, "0")
        return h > 0 ? sign + h + ":" + String(m).padStart(2, "0") + ":" + s : sign + m + ":" + s
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
            // Back to the view before the last move by hand: a zoom, a box, a
            // pan, a preset.
            IconToolButton {
                objectName: "plotBack"
                iconName: "undo"
                text: qsTr("Go back to the previous view")
                shortcutText: qsTr("Backspace")
                enabled: view.canGoBack
                disabledReason: qsTr("no earlier view")
                onClicked: view.goBack()
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
                        plot.traces ? plot.traces.appearanceRevision : 0
                        return view.curveColor(path)
                    }
                    readonly property string drawnAs: {
                        plot.traces ? plot.traces.appearanceRevision : 0
                        return plot.traces ? plot.transformText(plot.traces.multiplier(path), plot.traces.offset(path)) : ""
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
                    // Taken out of the Watchlist: its readings stay, none come.
                    readonly property bool gone: {
                        plot.shownTick
                        return plot.traces !== null && !plot.traces.watched(path)
                    }
                    readonly property string goneText: {
                        if (!gone || plot.cursorInView)
                            return ""
                        const at = plot.traces.lastReadUtcMs(path)
                        return isNaN(at) ? qsTr("not read") : qsTr("not read since %1").arg(Qt.formatTime(new Date(at), "HH:mm:ss"))
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
                    readonly property bool focused: view.focusedPath === path
                    implicitWidth: chipRow.implicitWidth + 2 * Type.padding
                    implicitHeight: Type.controlHeight
                    radius: Type.radius
                    color: focused ? Theme.stateSelectedInactive : Theme.surface
                    border.color: Theme.outlineVariant
                    RowLayout {
                        id: chipRow
                        anchors.fill: parent
                        anchors.leftMargin: Type.padding
                        anchors.rightMargin: Type.tight
                        spacing: Type.spacing
                        ToolButton {
                            id: curveMark
                            objectName: "plotChipSettings"
                            Layout.preferredWidth: Type.chipHeight
                            Layout.preferredHeight: Type.chipHeight
                            padding: 0
                            flat: true
                            focusPolicy: Qt.TabFocus
                            Accessible.name: qsTr("Curve settings for %1").arg(chip.path)
                            ToolTip.text: qsTr("Curve settings: color, multiplier and offset")
                            ToolTip.visible: hovered || visualFocus
                            ToolTip.delay: 700
                            onClicked: curveSettings.openFor(chip.path)
                            // The chip's menu from the keyboard.
                            Keys.onPressed: (event) => {
                                if (event.key === Qt.Key_Delete) {
                                    plot.traces.unplot(chip.path)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                    chipMenu.path = chip.path
                                    chipMenu.popup(curveMark, 0, curveMark.height)
                                    event.accepted = true
                                }
                            }
                            contentItem: Item {
                                implicitWidth: Type.iconSize
                                implicitHeight: Type.iconSize
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: parent.width
                                    height: 2
                                    color: chip.curve
                                }
                            }
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
                            // A click focuses the curve: the values fit it.
                            TapHandler {
                                onTapped: view.focusedPath = chip.focused ? "" : chip.path
                            }
                            PlainToolTip {
                                visible: nameHover.hovered
                                text: chip.focused ? qsTr("%1. Click to fit the values to every curve again.").arg(chip.path)
                                                   : qsTr("%1. Click to fit the values to this curve.").arg(chip.path)
                                delay: 700
                            }
                        }
                        // Drawn as other than read: says how, in the warning
                        // colour, so the line is never read as the value.
                        Rectangle {
                            objectName: "plotChipTransform"
                            visible: chip.drawnAs.length > 0
                            Layout.preferredWidth: transformText.implicitWidth + 2 * Type.tight
                            Layout.preferredHeight: Type.chipHeight - 2
                            radius: Type.radius
                            color: "transparent"
                            border.color: Theme.warning
                            Text {
                                id: transformText
                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                font.family: Type.fixedFamily
                                font.pointSize: Type.caption
                                color: Theme.warning
                                text: chip.drawnAs
                            }
                        }
                        Text {
                            objectName: "plotChipValue"
                            Layout.minimumWidth: valueWidth.advanceWidth
                            horizontalAlignment: Text.AlignRight
                            textFormat: Text.PlainText
                            color: chip.goneText.length > 0 ? Theme.textMuted : Theme.textPrimary
                            font.family: Type.fixedFamily
                            font.pointSize: Type.body
                            text: chip.goneText.length > 0 ? chip.goneText : chip.shown
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
                    // Its menu: right-click, or the Menu key on its settings.
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: chipMenu.openFor(chip.path)
                    }
                    Accessible.role: Accessible.StaticText
                    Accessible.description: chip.drawnAs.length > 0 ? qsTr("Drawn %1; the value is the reading").arg(chip.drawnAs) : ""
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
                gapColor: Theme.surfaceAlternate
                referenceColor: Theme.info
                dotRingColor: Theme.surfaceWindow
                font.family: Type.fixedFamily
                font.pointSize: Type.caption
                visible: plot.plotted.length > 0 && !plot.waiting
                // The graph's menu from the keyboard.
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                        graphMenu.at = -1
                        graphMenu.popup(view, view.plotArea.x + Type.padding, view.plotArea.y + Type.padding)
                        event.accepted = true
                    }
                }
                Accessible.role: Accessible.Graphic
                Accessible.name: {
                    const state = view.following ? qsTr("following")
                                                  : qsTr("%1 behind").arg(plot.behindWords(view.behindSeconds))
                    return plot.plotted.length === 1 ? qsTr("Plot of 1 value, %1").arg(state)
                                                     : qsTr("Plot of %1 values, %2").arg(plot.plotted.length).arg(state)
                }
                Accessible.description: qsTr("Arrows pan, plus and minus zoom, Shift+arrows step the cursor, Enter pins a reference, Home goes to the oldest reading, End follows, 1 to 4 choose the span; Up and Down move the values, Control with them zooms the values, A fits them again.")
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
            // The graph's menu: right-click.
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: (point) => {
                    graphMenu.at = point.position.x
                    graphMenu.popup()
                }
            }
            // Values zoomed or panned by hand: a way back to fitting them.
            Basic.Button {
                id: autoValues
                objectName: "plotAuto"
                visible: view.visible && !view.valueAuto
                x: view.valuesArea.left + Type.padding
                y: view.valuesArea.top + Type.padding
                height: Type.chipHeight
                padding: Type.tight
                leftPadding: Type.padding
                rightPadding: Type.padding
                focusPolicy: Qt.TabFocus
                font.pointSize: Type.caption
                text: qsTr("Auto (A)")
                onClicked: view.autoValues()
                Accessible.name: qsTr("Fit the values again")
                Accessible.description: qsTr("A, or a double-click on the value labels")
                contentItem: Text {
                    text: autoValues.text
                    font: autoValues.font
                    color: Theme.textPrimary
                    Accessible.ignored: true
                }
                background: Rectangle {
                    radius: Type.radius
                    color: autoValues.down ? Theme.statePressed : autoValues.hovered ? Theme.stateHover : Theme.surface
                    border.width: autoValues.visualFocus ? 2 : 1
                    border.color: autoValues.visualFocus ? Theme.focusRing : Theme.outline
                }
            }
            // Fitted to one curve: says which, on a chip so it reads over
            // the curves.
            Rectangle {
                objectName: "plotFittedChip"
                visible: view.visible && view.valueAuto && view.focusedPath.length > 0
                x: view.valuesArea.left + Type.padding
                y: view.valuesArea.top + Type.padding
                width: fitted.implicitWidth + 2 * Type.padding
                height: Type.chipHeight
                radius: Type.radius
                color: Theme.surface
                border.color: Theme.outlineVariant
                Label {
                    id: fitted
                    objectName: "plotFitted"
                    anchors.centerIn: parent
                    color: Theme.textSecondary
                    font.pointSize: Type.caption
                    text: qsTr("Fitted to %1").arg(view.focusedPath)
                }
            }
            // The reference's time, under the axis where it is pinned.
            Rectangle {
                id: referenceChip
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
                // Centred on its line, but never over the reference's chip:
                // beside it, 4 px apart, on the cursor's side.
                x: {
                    const centred = Math.max(0, Math.min(parent.width - width, view.cursorX - width / 2))
                    if (!referenceChip.visible)
                        return centred
                    const gap = 4
                    if (centred + width + gap <= referenceChip.x || centred >= referenceChip.x + referenceChip.width + gap)
                        return centred
                    const after = referenceChip.x + referenceChip.width + gap
                    const before = referenceChip.x - width - gap
                    if (view.cursorX >= view.referenceX)
                        return after + width <= parent.width ? after : Math.max(0, before)
                    return before >= 0 ? before : Math.min(parent.width - width, after)
                }
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
                        // From the start: the time since it, then the time of day.
                        const at = view.fromStart ? qsTr("%1 · %2").arg(plot.sinceText(view.cursorUtcMs - view.originUtcMs))
                                                                  .arg(plot.clockText(view.cursorUtcMs))
                                                  : plot.clockText(view.cursorUtcMs)
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
            // The time axis: the time of day, or since reading started (LP-1h).
            Basic.Button {
                id: timeAxis
                objectName: "plotTimeTitle"
                visible: view.visible
                anchors.right: parent.right
                anchors.rightMargin: Type.tight
                anchors.bottom: parent.bottom
                flat: true
                focusPolicy: Qt.TabFocus
                padding: Type.tight
                leftPadding: Type.padding
                rightPadding: Type.padding
                font.pointSize: Type.caption
                text: (view.fromStart ? qsTr("From start") : qsTr("Time of day")) + " \u25BE"
                contentItem: Text {
                    text: timeAxis.text
                    font: timeAxis.font
                    color: timeAxis.hovered || timeAxis.visualFocus ? Theme.textPrimary : Theme.textSecondary
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Type.radius
                    color: timeAxis.down ? Theme.statePressed : timeAxis.hovered ? Theme.stateHover : "transparent"
                    border.width: timeAxis.visualFocus ? 2 : 0
                    border.color: Theme.focusRing
                }
                Accessible.name: view.fromStart ? qsTr("Time axis: from the start of reading") : qsTr("Time axis: time of day")
                ToolTip.visible: hovered || visualFocus
                ToolTip.delay: 700
                ToolTip.text: view.fromStart
                              ? qsTr("Time since the Watchlist started reading, at %1 on this PC's clock").arg(plot.clockText(view.originUtcMs))
                              : qsTr("Time this PC received each reading (local time, %1)").arg(plot.utcOffset())
                onClicked: axisMenu.popup(timeAxis, 0, -axisMenu.implicitHeight)
                Menu {
                    id: axisMenu
                    objectName: "plotAxisMenu"
                    ButtonGroup { id: axisModes }
                    MenuItem {
                        objectName: "plotAxisTimeOfDay"
                        text: qsTr("Time of day")
                        checkable: true
                        checked: !view.fromStart
                        ButtonGroup.group: axisModes
                        onTriggered: view.fromStart = false
                    }
                    MenuItem {
                        objectName: "plotAxisFromStart"
                        text: qsTr("From the start of reading")
                        checkable: true
                        checked: view.fromStart
                        ButtonGroup.group: axisModes
                        onTriggered: view.fromStart = true
                    }
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
                    text: qsTr("Right-click a row and choose Plot, or press Ctrl+P on a row or a selection.")
                }
            }
        }
    }

    Menu {
        id: graphMenu
        objectName: "plotGraphMenu"
        // Where it was opened on the graph; -1 from the keyboard.
        property real at: -1
        MenuItem {
            objectName: "plotMenuFollow"
            text: qsTr("Follow") + "\t" + qsTr("End")
            checkable: true
            checked: view.following
            onTriggered: {
                view.following = !view.following
                checked = Qt.binding(() => view.following)
            }
        }
        MenuItem {
            objectName: "plotMenuBack"
            text: qsTr("Go back") + "	" + qsTr("Backspace")
            enabled: view.canGoBack
            onTriggered: view.goBack()
        }
        MenuItem {
            objectName: "plotMenuPin"
            text: qsTr("Pin reference here") + "\t" + qsTr("Enter")
            enabled: plot.plotted.length > 0
            onTriggered: {
                if (graphMenu.at >= view.plotArea.left)
                    view.setCursorAt(graphMenu.at)
                else if (!view.cursorShown)
                    view.stepCursor(0)
                view.pinReference()
            }
        }
        MenuItem {
            objectName: "plotMenuUnpin"
            text: qsTr("Remove reference") + "\t" + qsTr("Esc")
            enabled: view.referenceShown
            onTriggered: view.clearReference()
        }
        MenuSeparator {}
        MenuItem {
            objectName: "plotShowPoints"
            text: qsTr("Show measurement points")
            checkable: true
            checked: plot.traces !== null && plot.traces.showPoints
            onTriggered: {
                plot.traces.showPoints = !plot.traces.showPoints
                checked = Qt.binding(() => plot.traces !== null && plot.traces.showPoints)
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "plotMenuRemoveAll"
            text: qsTr("Remove all curves")
            enabled: plot.plotted.length > 0
            onTriggered: {
                // Their readings are kept, as with each chip's ×.
                for (const path of plot.plotted.slice())
                    plot.traces.unplot(path)
            }
        }
    }

    // One menu for a chip: what it was opened on.
    Menu {
        id: chipMenu
        objectName: "plotChipMenu"
        property string path: ""
        readonly property int points: {
            plot.traces ? plot.traces.appearanceRevision : 0
            return plot.traces && path.length > 0 ? plot.traces.points(path) : 0
        }
        function openFor(target) {
            path = target
            popup()
        }
        MenuItem {
            objectName: "plotChipFocus"
            text: view.focusedPath === chipMenu.path ? qsTr("Unfocus") : qsTr("Focus")
            onTriggered: view.focusedPath = view.focusedPath === chipMenu.path ? "" : chipMenu.path
        }
        MenuSeparator {}
        ButtonGroup { id: pointModes }
        MenuItem {
            objectName: "plotChipPointsPlot"
            text: qsTr("Points as the Plot")
            checkable: true
            checked: chipMenu.points === 0
            ButtonGroup.group: pointModes
            onTriggered: plot.traces.setPoints(chipMenu.path, 0)
        }
        MenuItem {
            objectName: "plotChipPointsShow"
            text: qsTr("Show points")
            checkable: true
            checked: chipMenu.points === 1
            ButtonGroup.group: pointModes
            onTriggered: plot.traces.setPoints(chipMenu.path, 1)
        }
        MenuItem {
            objectName: "plotChipPointsHide"
            text: qsTr("Hide points")
            checkable: true
            checked: chipMenu.points === 2
            ButtonGroup.group: pointModes
            onTriggered: plot.traces.setPoints(chipMenu.path, 2)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "plotChipSettingsItem"
            text: qsTr("Curve settings…")
            onTriggered: curveSettings.openFor(chipMenu.path)
        }
        MenuItem {
            objectName: "plotChipRemoveItem"
            text: qsTr("Remove from the Plot")
            onTriggered: plot.traces.unplot(chipMenu.path)
        }
    }

    // A curve's settings, in Trend's words: its colour, and the multiplier
    // and offset it is drawn with. Display only: the readings stay.
    Dialog {
        id: curveSettings
        objectName: "plotCurveSettings"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, (parent ? parent.width : 460) - 2 * Type.groupSpacing)
        modal: true
        title: qsTr("%1 curve").arg(path)
        closePolicy: Popup.CloseOnEscape
        property string path: ""
        readonly property bool validColor: /^#[0-9a-fA-F]{6}$/.test(colorField.text.trim())
        readonly property var multiplier: finiteDecimal(multiplierField.text)
        readonly property var offset: finiteDecimal(offsetField.text)
        readonly property bool valid: validColor && multiplier !== null && offset !== null
        function finiteDecimal(text) {
            const clean = text.trim()
            if (!/^[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?$/.test(clean))
                return null
            const value = Number(clean)
            return Number.isFinite(value) ? value : null
        }
        function openFor(target) {
            path = target
            colorField.text = String(view.curveColor(target)).toLowerCase()
            multiplierField.text = String(plot.traces.multiplier(target))
            offsetField.text = String(plot.traces.offset(target))
            open()
        }
        function apply() {
            if (!valid)
                return
            // Its palette colour is kept as no colour of its own.
            const own = colorField.text.trim().toLowerCase()
            const palette = plot.traces.colorIndex(path)
            const color = palette >= 0 && palette < plot.curveColors.length && plot.curveColors[palette] === own ? "" : own
            if (plot.traces.setAppearance(path, color, multiplier, offset))
                close()
        }
        onOpened: {
            multiplierField.forceActiveFocus(Qt.TabFocusReason)
            multiplierField.selectAll()
        }
        contentItem: ColumnLayout {
            spacing: Type.spacing
            Label { text: qsTr("Color"); color: Theme.textSecondary }
            GridLayout {
                Layout.fillWidth: true
                columns: 8
                columnSpacing: Type.tight
                rowSpacing: Type.tight
                Repeater {
                    model: plot.curveColors
                    delegate: ToolButton {
                        id: swatch
                        required property string modelData
                        required property int index
                        objectName: "plotColorPreset_" + index
                        implicitWidth: Type.controlHeight
                        implicitHeight: Type.controlHeight
                        Accessible.name: qsTr("Use color %1").arg(modelData)
                        onClicked: colorField.text = swatch.modelData
                        contentItem: Rectangle {
                            implicitWidth: Type.iconSize
                            implicitHeight: Type.iconSize
                            color: swatch.modelData
                            radius: Type.radius
                            border.width: colorField.text.toLowerCase() === swatch.modelData ? 2 : 0
                            border.color: Theme.textPrimary
                        }
                    }
                }
            }
            TextField {
                id: colorField
                objectName: "plotCurveColor"
                Layout.fillWidth: true
                maximumLength: 7
                selectByMouse: true
                Accessible.name: qsTr("Curve color in hexadecimal")
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Display only: y = k × reading + b")
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Type.spacing
                rowSpacing: Type.spacing
                Label { text: qsTr("Multiplier k"); color: Theme.textSecondary }
                TextField {
                    id: multiplierField
                    objectName: "plotCurveMultiplier"
                    Layout.fillWidth: true
                    selectByMouse: true
                    Accessible.name: qsTr("Display multiplier k")
                    onAccepted: curveSettings.apply()
                }
                Label { text: qsTr("Offset b"); color: Theme.textSecondary }
                TextField {
                    id: offsetField
                    objectName: "plotCurveOffset"
                    Layout.fillWidth: true
                    selectByMouse: true
                    Accessible.name: qsTr("Display offset b")
                    onAccepted: curveSettings.apply()
                }
            }
            Button {
                objectName: "plotCurveResetTransform"
                text: qsTr("Reset transform")
                flat: true
                Layout.leftMargin: -leftPadding
                onClicked: {
                    multiplierField.text = "1"
                    offsetField.text = "0"
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("The readings stay as they were read; the chip and the cursor give them. All plotted numbers share one value axis.")
                color: Theme.textMuted
                font.pointSize: Type.caption
                wrapMode: Text.Wrap
            }
            Label {
                objectName: "plotCurveSettingsProblem"
                Layout.fillWidth: true
                visible: text.length > 0
                text: !curveSettings.validColor ? qsTr("Use a six-digit color, for example #58a6ff.")
                    : curveSettings.multiplier === null || curveSettings.offset === null
                        ? qsTr("Enter finite numbers for k and b.") : ""
                textFormat: Text.PlainText
                color: Theme.error
                wrapMode: Text.Wrap
            }
        }
        footer: DialogButtonBox {
            Button {
                objectName: "plotCurveApply"
                text: qsTr("Apply")
                enabled: curveSettings.valid
                DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
                onClicked: curveSettings.apply()
            }
            Button {
                objectName: "plotCurveCancel"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: curveSettings.close()
            }
        }
    }
}
