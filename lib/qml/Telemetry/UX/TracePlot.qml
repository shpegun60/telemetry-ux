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
// graph's menu shows every curve's points. In a narrow dock the header gives
// way step by step (narrowStep), down to Follow's icon and "1 min ▾"; Back
// and Measure stay. The Trend (TR-2) draws a recorded run with it:
// `recording` gives it the run's words and rules, and the host's own controls
// take the ends of the header row. Presentation only:
// WatchTraces keeps the readings and TraceView draws them. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Dialogs
import QtQuick.Layouts

FocusScope {
    id: plot
    // A provider's WatchTraces (provider.watchTraces); null shows nothing.
    property var traces: null
    // The provider, optional: with it, values dropped from the Device tree
    // join the Watchlist first, so they are read and plotted (LP-1g3).
    property var provider: null
    // The measurements (analysis, M): a table of every curve over the time
    // in view, or from the reference to the end, in place of the chips.
    property bool measuring: false
    // A recorded run (TR-2, the Trend) rather than what the Watchlist reads:
    // its words say so - × leaves the recording as it is, the axis counts
    // from the run's start - a chip with no cursor gives the reading at the
    // right edge of the view, nothing is dropped on it, and Follow is offered
    // only while the run is still being written. The header gives way in a
    // narrow dock as the live Plot's does (narrowStep), and the chips keep to
    // one row in a short one.
    property bool recording: false
    property bool writing: false
    readonly property bool followable: !recording || writing
    // "today 22:26": the run, for the graph's name to a screen reader.
    property string runName: ""
    // The host's own controls at either end of the header row: the Trend's
    // run and + Value before Follow, its tag after Measure. The leading ones
    // keep their width while the Plot's own give way; the trailing ones give
    // way after Follow's icon, before the axis' words (narrowStep).
    property alias leading: leadingSlot.data
    property alias trailing: trailingSlot.data
    // What the graph says with nothing plotted, while the plotted values have
    // no readings yet, and over the graph when the host has something to say
    // ("No readings here").
    property string emptyText: traces ? qsTr("Add a value from the Watchlist to plot it.")
                                      : qsTr("Connect to a device to plot its values.")
    property string emptyHint: qsTr("Right-click a row and choose Plot, press Ctrl+P, or drag rows here.")
    property string waitingText: plotted.length === 1 ? qsTr("Waiting for the first reading of %1…").arg(plotted[0])
                                                      : qsTr("Waiting for the first readings…")
    property string message: ""
    // A recorded run below 400 px of height: the chips keep to one row and
    // scroll.
    readonly property bool oneChipRow: recording && height < 400
    // A time as the axis counts it: "17.911 s from run start", or the time of
    // day.
    function axisWords(ms) {
        return view.fromStart ? qsTr("%1 from run start").arg(sinceText(ms - view.originUtcMs)) : clockText(ms)
    }
    // "62.4 %"
    function percentText(share) {
        return qsTr("%1 %").arg((share * 100).toFixed(1))
    }
    // A state's row: "true 62.4 % of the time · 14 changes", or each state's
    // share, the largest first.
    function sharesText(stats) {
        if (!stats || !stats.shares || stats.shares.length === 0)
            return qsTr("No readings in this range")
        const changes = stats.changes === 1 ? qsTr("1 change") : qsTr("%L1 changes").arg(stats.changes)
        if (stats.shares.length <= 2 && stats.shares.some(s => s.name === "true")) {
            const yes = stats.shares.find(s => s.name === "true")
            return qsTr("true %1 of the time · %2").arg(percentText(yes ? yes.share : 0)).arg(changes)
        }
        return stats.shares.slice(0, 4).map(s => s.name + " " + percentText(s.share)).join(" · ") + " · " + changes
    }
    // What the last drop, copy, save or find did. "Copied …", "Saved …" and
    // a find that found nothing leave after 5 s, and a find that found its
    // change takes a find's words away; a refusal, and a file that could not
    // be written, stay until ×.
    property string notice: ""
    // How the notice leaves: "done" and "find" after 5 s, "refusal" by ×.
    property string noticeKind: ""
    function tell(words, kind) {
        noticeKind = kind
        notice = words
        noticeTimer.restart()
    }
    // One timer, started again by every notice.
    Timer {
        id: noticeTimer
        interval: 5000
        onTriggered: {
            if (plot.noticeKind !== "refusal")
                plot.notice = ""
        }
    }
    Connections {
        target: view
        function onFindFailed(words) { plot.tell(words, "find") }
        function onChangeFound() {
            if (plot.noticeKind === "find")
                plot.notice = ""
        }
    }
    // Each number in a lane of its own (L, the graph's menu): the Plot's
    // setting, kept with its look and one step to undo. Only while every lane
    // can be 40 px tall; otherwise it says why and changes nothing.
    readonly property string stackRefusal: qsTr("Too many numbers to stack at this height. Make the Plot taller or remove a curve.")
    function toggleStack() {
        if (!plot.traces)
            return
        if (!plot.traces.stacked && !view.stackFits) {
            plot.tell(plot.stackRefusal, "find")
            return
        }
        plot.traces.stacked = !plot.traces.stacked
    }
    // The graph with its table or chips, as shown, onto the clipboard: not
    // the toolbar, the notice or the pointer's cursor; the reference, the
    // end and their chips stay.
    function copyPicture() {
        view.hideCursor()
        plotPicture.grabToImage(result => {
            if (Clipboard.setImage(result.image))
                plot.tell(qsTr("Copied the picture."), "done")
            else
                plot.tell(qsTr("The picture could not be copied."), "refusal")
        })
    }
    // The readings in view, for a spreadsheet: tabs, this PC's decimal sign.
    function copyValues() {
        const readings = view.readingsInRange()
        Clipboard.setText(view.valuesText(false))
        tell(qsTr("Copied %L1 readings of %L2 values.").arg(readings).arg(plotted.length), "done")
    }
    // "plot 2026-11-15 12-04-31.csv"
    function csvName() {
        return "plot " + Qt.formatDateTime(new Date(), "yyyy-MM-dd HH-mm-ss") + ".csv"
    }
    function saveValuesTo(url) {
        const readings = view.readingsInRange()
        const why = view.saveValues(url)
        const name = String(url).replace(/^.*[\\/]/, "")
        if (why.length > 0)
            tell(why, "refusal")
        else
            tell(qsTr("Saved %L1 readings of %L2 values to %3.").arg(readings).arg(plotted.length).arg(name), "done")
        return why.length === 0
    }
    FileDialog {
        id: saveDialog
        objectName: "plotSaveDialog"
        title: view.referenceShown && view.endShown ? qsTr("Save the values from the reference to the end")
                                                    : qsTr("Save the values in view")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Comma-separated values (*.csv)")]
        defaultSuffix: "csv"
        onAccepted: plot.saveValuesTo(selectedFile)
    }
    // Puts dropped values on the Plot: a Watchlist drag's paths are its
    // values; a Device tree drag's are Fields, Commands, Services or values
    // inside a Field, added to the Watchlist and plotted value by value.
    function dropPaths(payload) {
        // Any list of paths: an array from a drop's JSON, or a host's list.
        const dropped = payload && payload.paths ? Array.from(payload.paths) : []
        if (!traces || dropped.length === 0)
            return false
        let values = dropped
        if (payload.from === "device") {
            if (!provider || typeof provider.addWatchPaths !== "function" || typeof provider.watchBindings !== "function") {
                tell(qsTr("Add these to the Watchlist first; the Plot draws what the Watchlist reads."), "refusal")
                return false
            }
            provider.addWatchPaths(dropped)
            // The Watchlist's values under what was dropped, as it names them.
            values = []
            for (const binding of provider.watchBindings()) {
                const member = typeof binding.member === "string" ? binding.member : ""
                const path = binding.group + "." + binding.name + (member.length === 0 ? "" : member.startsWith("[") ? member : "." + member)
                if (dropped.some(at => path === at || path.startsWith(at + ".") || path.startsWith(at + "[")))
                    values.push(path)
            }
        }
        // Text is passed over and counted; the rest go on as one step of the
        // history, and only the Plot's sixteen curves end them.
        const fresh = values.filter(path => !traces.isPlotted(path))
        const texts = fresh.filter(path => traces.refusal(path).length > 0).length
        const result = traces.plotMany(fresh.filter(path => traces.refusal(path).length === 0))
        const added = result.added, refusal = result.refusal
        const textWords = texts === 0 ? ""
                        : values.length === 1 ? traces.refusal(values[0])
                        : texts === 1 ? qsTr("1 of %L1 values is text and was not plotted.").arg(values.length)
                        : qsTr("%L1 of %L2 values are text and were not plotted.").arg(texts).arg(values.length)
        const words = [refusal, textWords].filter(part => part.length > 0).join(" ")
        if (words.length > 0)
            tell(words, "refusal")
        else if (values.length === 0)
            tell(qsTr("Nothing here can be plotted: the Plot draws values the Watchlist reads."), "refusal")
        else if (added === 0)
            tell(values.length === 1 ? qsTr("%1 is on the Plot already.").arg(values[0])
                                     : qsTr("These %L1 values are on the Plot already.").arg(values.length), "refusal")
        else
            notice = ""
        return added > 0
    }
    // What a drop will do, said while a drag is over the Plot. A Device tree
    // branch joins the Watchlist whole - often more than the sixteen curves
    // the Plot draws - so it says so.
    function dropWords(payload) {
        const paths = payload && payload.paths ? Array.from(payload.paths) : []
        if (paths.length === 0)
            return ""
        if (payload.from === "device")
            return paths.length === 1 ? qsTr("Drop to add %1 to the Watchlist and plot it").arg(paths[0])
                                      : qsTr("Drop to add %L1 to the Watchlist and plot them").arg(paths.length)
        return paths.length === 1 ? qsTr("Drop to plot %1").arg(paths[0]) : qsTr("Drop to plot %L1 values").arg(paths.length)
    }
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
    // The spans the header offers; keys 1 to 4 choose them on the graph.
    readonly property var spans: [
        { seconds: 10, text: qsTr("10 s"), name: qsTr("Last 10 seconds") },
        { seconds: 60, text: qsTr("1 min"), name: qsTr("Last minute") },
        { seconds: 600, text: qsTr("10 min"), name: qsTr("Last 10 minutes") },
        { seconds: 0, text: qsTr("All"), name: plot.recording ? qsTr("The whole run") : qsTr("Everything kept") }
    ]
    // "10 s", or a span zoomed to by hand: "0.5 s", "48 s", "12 min", "1.5 h".
    function spanWords() {
        const chosen = spans.find(span => span.seconds === view.preset)
        if (chosen)
            return chosen.text
        const seconds = view.spanSeconds
        if (seconds < 10)
            return qsTr("%1 s").arg(Number(seconds.toPrecision(2)))
        if (seconds < 120)
            return qsTr("%1 s").arg(Math.round(seconds))
        if (seconds < 3600)
            return qsTr("%1 min").arg(Math.round(seconds / 60))
        return qsTr("%1 h").arg(Number((seconds / 3600).toPrecision(2)))
    }
    // What gives way as the Plot narrows, in this order: the zoom buttons (the
    // wheel, + and - still zoom); then the spans, folded into one button that
    // shows the span, "10 s ▾", the four in its menu (1 to 4 still choose);
    // then Follow, as its icon (End still follows); then the host's trailing
    // controls; and last the time axis' words, "Clock ▾" or "Start ▾". Back
    // and Measure always stay, and the host's leading controls keep their
    // width until everything else has given way. Measured from the controls,
    // as the Watchlist's toolbar is, so it holds at any zoom and in any
    // style: 0 is the whole header, 4 everything given way.
    readonly property real headerRoom: width - 2 * Type.padding
    // The host's slots: the leading at its own width, the trailing while it
    // holds something to show.
    readonly property real leadingNeeds: leadingSlot.visible ? leadingSlot.implicitWidth + Type.groupSpacing : 0
    readonly property real trailingNeeds: trailingSlot.implicitWidth > 0 ? Type.groupSpacing + trailingSlot.implicitWidth : 0
    function headerNeeds(followWidth, spanWidth, zoom, trailing) {
        const gap = Type.groupSpacing
        return leadingNeeds + (followable ? followWidth + gap : 0) + spanWidth + gap
               + (zoom ? zoomOut.implicitWidth + gap + zoomIn.implicitWidth + gap : 0)
               + back.implicitWidth + gap + (measureToggle.visible ? gap + measureToggle.implicitWidth : 0)
               + (trailing ? trailingNeeds : 0)
    }
    // The header with everything given way: narrower than this, it is cut.
    readonly property real headerMinimum: headerNeeds(followIcon.implicitWidth, spanButton.implicitWidth, false, false)
    // With Follow's icon, the trailing controls still there.
    readonly property real headerCompact: headerNeeds(followIcon.implicitWidth, spanButton.implicitWidth, false, true)
    // The step the controls need now; narrowStep is the step shown.
    readonly property int narrowNeed: {
        if (headerRoom >= headerNeeds(follow.implicitWidth, presets.implicitWidth, true, true))
            return 0
        if (headerRoom >= headerNeeds(follow.implicitWidth, presets.implicitWidth, false, true))
            return 1
        const folded = headerNeeds(follow.implicitWidth, spanButton.implicitWidth, false, true)
        if (headerRoom >= folded)
            return 2
        // The axis gives back its words last: once Follow is an icon, as the
        // header comes within what the shorter words save of its narrowest,
        // and not before the trailing controls have gone.
        const brief = Math.min(folded - 1, headerMinimum + axisSaving, trailingNeeds > 0 ? headerCompact : Infinity)
        return headerRoom >= brief ? 3 : 4
    }
    // The trailing controls give way once Follow is an icon and they no
    // longer fit beside it.
    readonly property bool trailingNeedsToGo: narrowNeed >= 3 && headerRoom < headerCompact
    // What is shown folds at once when the controls no longer fit, but
    // unfolds only when the Plot's own width changes. A host's words that
    // change as it works ("5 reads not saved"), or Follow coming and going,
    // would otherwise fold and unfold the spans on every change near a
    // threshold, and the header row and the chips under it would jump.
    // Ordered as one scale: step 3 with the trailing controls, step 3
    // without them, step 4.
    property int narrowStep: 0
    property bool trailingGivesWay: false
    function takeNarrowStep(always) {
        const need = 2 * narrowNeed + (trailingNeedsToGo ? 1 : 0)
        if (always || need > 2 * narrowStep + (trailingGivesWay ? 1 : 0)) {
            narrowStep = narrowNeed
            trailingGivesWay = trailingNeedsToGo
        }
    }
    function retakeNarrowStep() {
        takeNarrowStep(true)
    }
    onNarrowNeedChanged: takeNarrowStep(false)
    onTrailingNeedsToGoChanged: takeNarrowStep(false)
    // After the bindings that hang on the width have settled.
    onHeaderRoomChanged: Qt.callLater(retakeNarrowStep)
    Component.onCompleted: retakeNarrowStep()
    // Once more as the start-up settles: fonts, styles and the host's slots
    // arrive after completion.
    Timer {
        interval: 300
        running: true
        onTriggered: plot.retakeNarrowStep()
    }
    readonly property real axisSaving: {
        void axisMetrics.font
        const width = words => Math.max(...words.map(word => axisMetrics.advanceWidth(word + " ▾")))
        const full = [qsTr("Time of day"), recording ? qsTr("From run start (s)") : qsTr("From start")]
        return Math.max(0, width(full) - width([qsTr("Clock"), qsTr("Start")]))
    }
    // The graph's least height: the legend and the table scroll before it
    // shrinks below this.
    readonly property real graphMinimumHeight: 6 * Type.rowHeight
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
    // A line drawn at x is on the plot area: lines sit on whole pixels, so
    // one on the newest reading may be half a pixel past its right edge.
    function onPlot(x) {
        return x >= view.plotArea.left - 1 && x <= view.plotArea.right + 1
    }
    // The cursor is in the picture: the chips show their values there. Once
    // its reading scrolls out they go back to the newest.
    readonly property bool cursorInView: view.cursorShown && plot.onPlot(view.cursorX)
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
    // The decimals a number's text shows: "4.95" 2, "−100" 0; -1 for one in
    // powers of ten ("1e-7") or none ("–").
    function decimalsOf(text) {
        if (!/^[−-]?\d+(\.\d+)?$/.test(text))
            return -1
        const at = text.indexOf(".")
        return at < 0 ? 0 : text.length - at - 1
    }
    // value to so many decimals, with the typographic minus and never "−0.0".
    function fixedText(value, decimals) {
        if (value === undefined || value === null || isNaN(value))
            return "–"
        let text = value.toFixed(decimals)
        if (Number(text) === 0)
            text = text.replace("-", "")
        return text.startsWith("-") ? "−" + text.substring(1) : text
    }
    // Where a chip of width w for the line at lineX sits under the axis:
    // centred on its line, else beside a chip it would cover, 4 px apart -
    // the free place nearest the centre.
    function chipX(lineX, w, bound, others) {
        const centred = Math.max(0, Math.min(bound - w, lineX - w / 2))
        const gap = 4
        const shown = others.filter(other => other.visible)
        const free = at => at >= 0 && at + w <= bound
                           && shown.every(other => at + w + gap <= other.x || at >= other.x + other.width + gap)
        if (free(centred))
            return centred
        let best = centred, distance = Infinity
        for (const other of shown)
            for (const at of [other.x + other.width + gap, other.x - w - gap])
                if (free(at) && Math.abs(at - centred) < distance) {
                    best = at
                    distance = Math.abs(at - centred)
                }
        return best
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
            RowLayout {
                id: leadingSlot
                objectName: "plotLeading"
                visible: children.length > 0
                Layout.fillWidth: plot.recording
                Layout.minimumWidth: 0
                spacing: Type.spacing
            }
            CheckBox {
                id: follow
                objectName: "plotFollow"
                visible: plot.followable && plot.narrowStep < 3
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
            // Narrow: Follow as its icon, filled while following.
            IconToolButton {
                id: followIcon
                objectName: "plotFollowIcon"
                visible: plot.followable && plot.narrowStep >= 3
                iconName: "follow"
                text: qsTr("Follow the newest reading")
                shortcutText: qsTr("End")
                checkable: true
                checked: view.following
                switchedOn: checked
                toggles: true
                onToggled: view.following = checked
                Accessible.description: qsTr("Keep the newest readings in view. Off, the picture holds still; reading goes on.")
            }
            // 10 s | 1 min | 10 min | All, as the Watchlist's List | Tree:
            // the chosen one filled, Left and Right move between them.
            Row {
                id: presets
                objectName: "plotPresets"
                visible: plot.narrowStep < 2
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("Time span")
                Layout.preferredHeight: Type.controlHeight
                spacing: 0
                ButtonGroup { id: presetGroup }
                Repeater {
                    id: presetButtons
                    model: plot.spans
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
            // Too narrow for four: one button showing the span, "10 s ▾", as
            // the Watchlist's "Hex ▾"; as wide as its widest word, the four in
            // its menu.
            Button {
                id: spanButton
                objectName: "plotSpanButton"
                visible: plot.narrowStep >= 2
                FontMetrics { id: spanMetrics; font: spanButton.font }
                implicitWidth: {
                    void spanMetrics.font
                    const words = plot.spans.map(span => span.text).concat([plot.spanWords()])
                    return Math.ceil(Math.max(...words.map(word => spanMetrics.advanceWidth(word + " ▾"))))
                           + leftPadding + rightPadding
                }
                Layout.preferredHeight: Type.controlHeight
                verticalPadding: 0
                flat: true
                focusPolicy: Qt.TabFocus
                font.pointSize: Type.caption
                text: plot.spanWords() + " ▾"
                onClicked: spanMenu.popup(spanButton, 0, spanButton.height)
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: qsTr("Time span: %1").arg(plot.spanWords())
                ToolTip.visible: (hovered || visualFocus) && !spanMenu.visible
                ToolTip.delay: 700
                ToolTip.text: qsTr("The time in view; keys 1 to 4 choose it")
                Menu {
                    id: spanMenu
                    objectName: "plotSpanMenu"
                    ButtonGroup { id: spanItems; exclusive: true }
                    Repeater {
                        model: plot.spans
                        MenuItem {
                            required property var modelData
                            required property int index
                            objectName: "plotSpanItem_" + modelData.seconds
                            text: modelData.text + "\t" + (index + 1)
                            checkable: true
                            checked: view.preset === modelData.seconds
                            ButtonGroup.group: spanItems
                            Accessible.name: modelData.name
                            onTriggered: view.choosePreset(modelData.seconds)
                        }
                    }
                }
            }
            // The wheel, + and - zoom as well: the first to give way.
            IconToolButton {
                id: zoomOut
                objectName: "plotZoomOut"
                visible: plot.narrowStep < 1
                iconName: "zoomOut"
                text: qsTr("Zoom out")
                shortcutText: qsTr("Minus")
                onClicked: view.zoom(1.25)
            }
            IconToolButton {
                id: zoomIn
                objectName: "plotZoomIn"
                visible: plot.narrowStep < 1
                iconName: "zoomIn"
                text: qsTr("Zoom in")
                shortcutText: qsTr("Plus")
                onClicked: view.zoom(0.8)
            }
            // Back to the view before the last move by hand: a zoom, a box, a
            // pan, a preset.
            IconToolButton {
                id: back
                objectName: "plotBack"
                iconName: "undo"
                text: qsTr("Go back to the previous view")
                shortcutText: qsTr("Backspace")
                enabled: view.canGoBack
                disabledReason: qsTr("no earlier view")
                onClicked: view.goBack()
            }
            Item { Layout.fillWidth: true }
            // A segment as the spans are: filled while on, the header's height.
            Button {
                id: measureToggle
                objectName: "plotMeasureToggle"
                visible: plot.plotted.length > 0
                Layout.preferredHeight: Type.controlHeight
                verticalPadding: 0
                font.pointSize: Type.caption
                text: qsTr("Measure")
                checkable: true
                checked: plot.measuring
                flat: !checked
                focusPolicy: Qt.TabFocus
                onToggled: plot.measuring = checked
                ToolTip.visible: hovered || visualFocus
                ToolTip.delay: 700
                ToolTip.text: qsTr("Measure every curve over the time in view, or from the reference to the end (M). Shift and a click pin the end.")
            }
            RowLayout {
                id: trailingSlot
                objectName: "plotTrailing"
                // Gives way after Follow's icon, before the axis' words.
                visible: children.length > 0 && !plot.trailingGivesWay
                spacing: Type.spacing
            }
        }

        // What the last drop did, said once, until the next one or ×.
        RowLayout {
            objectName: "plotNotice"
            visible: plot.notice.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: Type.padding
            Layout.rightMargin: Type.padding
            spacing: Type.spacing
            Label {
                objectName: "plotNoticeText"
                Layout.fillWidth: true
                text: plot.notice
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                font.pointSize: Type.caption
                Accessible.role: Accessible.AlertMessage
                Accessible.name: plot.notice
            }
            ToolButton {
                objectName: "plotNoticeClose"
                Layout.preferredWidth: Type.chipHeight
                Layout.preferredHeight: Type.chipHeight
                padding: 0
                flat: true
                focusPolicy: Qt.TabFocus
                text: qsTr("×")
                Accessible.name: qsTr("Dismiss")
                onClicked: plot.notice = ""
            }
        }
        // What Copy picture takes: the table or the chips, and the graph.
        ColumnLayout {
            id: plotPicture
            objectName: "plotPicture"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Type.spacing

            // The measurements: what range, then a row per curve.
            ColumnLayout {
                id: measureTable
                objectName: "plotMeasure"
                visible: plot.measuring && plot.plotted.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: Type.padding
                Layout.rightMargin: Type.padding
                spacing: 0
                readonly property var range: {
                    plot.shownTick
                    view.cursorShown
                    view.referenceShown
                    view.endShown
                    return measureTable.visible ? view.measureRange() : ({})
                }
                readonly property real cell: Math.max(valueCell.advanceWidth, headerCell.advanceWidth) + Type.spacing
                // The Name column keeps nine characters of the body at the least:
                // all seven figures when they fit beside it, else five, else three.
                readonly property real nameWidth: Type.bodyPixels * 9
                // The scroll bar's room while the rows scroll.
                readonly property real barRoom: measureRows.scrolls ? measureBar.width : 0
                readonly property int columns: {
                    const room = width - 2 * Type.chipHeight - Type.spacing - barRoom - nameWidth
                    return room >= 7 * cell ? 7 : room >= 5 * cell ? 5 : 3
                }
                // The figures shown, in order; a row's tooltip gives the others.
                readonly property var keys: columns === 7 ? ["now", "delta", "min", "max", "mean", "pkpk", "std"]
                                          : columns === 5 ? ["now", "delta", "min", "max", "mean"] : ["now", "min", "max"]
                // The cells a state's shares span: every figure after its reading
                // and its empty Δ.
                readonly property int sharesCells: columns - (keys.indexOf("delta") >= 0 ? 2 : 1)
                // A column's title. The first says whose reading it is: the
                // cursor's while the cursor is in view, else the end's while one is
                // pinned, else the newest.
                function title(key) {
                    switch (key) {
                    case "now":
                        return plot.cursorInView ? qsTr("At cursor") : view.endShown ? qsTr("At end") : qsTr("Newest")
                    case "delta":
                        return "Δ"
                    case "min":
                        return qsTr("Min")
                    case "max":
                        return qsTr("Max")
                    case "mean":
                        return qsTr("Mean")
                    case "pkpk":
                        return qsTr("Pk-pk")
                    default:
                        return qsTr("Std dev")
                    }
                }
                TextMetrics { id: valueCell; font.family: Type.fixedFamily; font.pointSize: Type.caption; text: "−0.000000" }
                TextMetrics { id: headerCell; font.pointSize: Type.caption; text: qsTr("At cursor") }
                Label {
                    objectName: "plotMeasureRange"
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textSecondary
                    font.pointSize: Type.caption
                    text: !measureTable.range.toUtcMs ? ""
                        : (measureTable.range.pinned ? qsTr("Reference to end") : qsTr("In view")) + " · "
                          + plot.clockText(measureTable.range.fromUtcMs) + " – " + plot.clockText(measureTable.range.toUtcMs) + " · "
                          + plot.spanText(measureTable.range.seconds * 1000) + " · "
                          + qsTr("%L1 readings").arg(measureTable.range.readings)
                          + (measureTable.range.failed > 0 ? " · " + (measureTable.range.failed === 1 ? qsTr("1 failed read")
                                                                                         : qsTr("%L1 failed reads").arg(measureTable.range.failed)) : "")
                }
                // The reference is older than the readings kept.
                Label {
                    objectName: "plotMeasureTrimmed"
                    Layout.fillWidth: true
                    visible: measureTable.range.keptFromUtcMs !== undefined
                    textFormat: Text.PlainText
                    wrapMode: Text.WordWrap
                    color: Theme.warning
                    font.pointSize: Type.caption
                    text: visible ? qsTr("Readings before %1 are no longer kept; measured from there.")
                                      .arg(plot.clockText(measureTable.range.keptFromUtcMs)) : ""
                }
                // The column titles.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Item { Layout.preferredWidth: Type.chipHeight }
                    Text { Layout.fillWidth: true; text: qsTr("Name"); color: Theme.textSecondary; font.pointSize: Type.caption }
                    Repeater {
                        model: measureTable.keys
                        delegate: Text {
                            required property string modelData
                            objectName: "plotMeasureTitle_" + modelData
                            Layout.preferredWidth: measureTable.cell
                            horizontalAlignment: Text.AlignRight
                            text: measureTable.title(modelData)
                            color: Theme.textSecondary
                            font.pointSize: Type.caption
                            readonly property string tip: {
                                switch (modelData) {
                                case "mean":
                                    return qsTr("Mean of the %L1 readings").arg(measureTable.range.readings)
                                case "pkpk":
                                    return qsTr("Peak to peak: Max − Min")
                                case "std":
                                    return qsTr("Spread about the mean: the AC RMS of the readings")
                                case "delta":
                                    return qsTr("Change from the reference")
                                default:
                                    return ""
                                }
                            }
                            HoverHandler { id: titleHover }
                            PlainToolTip {
                                objectName: "plotMeasureTitleTip"
                                visible: titleHover.hovered && parent.tip.length > 0
                                delay: 500
                                text: parent.tip
                            }
                        }
                    }
                    Item {
                        Layout.leftMargin: Type.spacing
                        Layout.preferredWidth: Type.chipHeight + measureTable.barRoom
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.outlineVariant }
                // A row per curve: three at the least, else up to 40 % of the
                // Plot's height, then they scroll.
                ListView {
                    id: measureRows
                    objectName: "plotMeasureRows"
                    Layout.fillWidth: true
                    Layout.preferredHeight: count * Type.rowHeight
                    Layout.maximumHeight: Math.max(3 * Type.rowHeight, plot.height * 0.4)
                    readonly property bool scrolls: contentHeight > height + 0.5
                    clip: true
                    interactive: scrolls
                    boundsBehavior: Flickable.StopAtBounds
                    model: plot.plotted
                    ScrollBar.vertical: ScrollBar {
                        id: measureBar
                        objectName: "plotMeasureScrollBar"
                        policy: ScrollBar.AsNeeded
                    }
                    delegate: Rectangle {
                        id: measureRow
                        activeFocusOnTab: true
                        border.width: activeFocus ? 2 : 0
                        border.color: Theme.focusRing
                        // The chip menu from the keyboard: Go to minimum / maximum.
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                                chipMenu.path = measureRow.path
                                chipMenu.popup(measureRow, Type.chipHeight, measureRow.height)
                                event.accepted = true
                            }
                        }
                        required property string modelData
                        required property int index
                        objectName: "plotMeasureRow"
                        readonly property string path: modelData
                        readonly property bool states: plot.traces ? plot.traces.categorical(path) : false
                        readonly property var stats: {
                            plot.shownTick
                            measureTable.range
                            return measureTable.visible ? view.statistics(path) : ({})
                        }
                        // The reading at the cursor while it is in view, else at the
                        // end while one is pinned, else the newest.
                        readonly property var current: {
                            plot.shownTick
                            view.cursorUtcMs
                            view.endUtcMs
                            view.rightUtcMs
                            if (!plot.traces)
                                return undefined
                            return plot.cursorInView ? view.valueAt(path)
                                 : view.endShown ? view.endValue(path)
                                 : plot.recording ? view.valueAtRight(path) : plot.traces.latest(path)
                        }
                        readonly property var reference: {
                            view.referenceShown
                            view.referenceUtcMs
                            return view.referenceValue(path)
                        }
                        readonly property string drawnAs: {
                            plot.traces ? plot.traces.appearanceRevision : 0
                            return plot.traces ? plot.transformText(plot.traces.multiplier(path), plot.traces.offset(path)) : ""
                        }
                        // Mean and Std dev to one decimal more than Min and Max
                        // give; -1 when those are not plain decimals.
                        readonly property int decimals: {
                            const low = plot.decimalsOf(number(stats.min)), high = plot.decimalsOf(number(stats.max))
                            return low < 0 || high < 0 ? -1 : Math.max(low, high) + 1
                        }
                        // Every figure's text. A state gives its reading in words
                        // and no difference.
                        readonly property var cells: ({
                            now: states ? (current === undefined ? "–" : plot.traces.stateText(path, current)) : number(current),
                            delta: states || reference === undefined || current === undefined ? ""
                                                                                             : plot.deltaText(current - reference),
                            min: number(stats.min),
                            max: number(stats.max),
                            mean: spread(stats.mean),
                            pkpk: number(stats.peakToPeak),
                            std: spread(stats.deviation)
                        })
                        // What a narrow table leaves out, for the row's tooltip:
                        // "Δ +2.4 · Mean 4.95 · Pk-pk 2.3 · Std dev 0.69".
                        readonly property string hidden: {
                            if (states)
                                return ""
                            const names = {delta: "Δ", mean: qsTr("Mean"), pkpk: qsTr("Pk-pk"), std: qsTr("Std dev")}
                            const parts = []
                            for (const key of ["delta", "mean", "pkpk", "std"])
                                if (measureTable.keys.indexOf(key) < 0 && cells[key].length > 0 && cells[key] !== "–")
                                    parts.push(names[key] + " " + cells[key])
                            return parts.join(" · ")
                        }
                        // Min or Max under the pointer: their tooltip, not the row's.
                        property int extremeHovers: 0
                        function number(value) { return plot.valueText(value) }
                        function spread(value) { return decimals < 0 ? number(value) : plot.fixedText(value, decimals) }
                        width: measureRows.width - measureTable.barRoom
                        height: Type.rowHeight
                        color: index % 2 ? Theme.surfaceAlternate : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            spacing: 0
                            Item {
                                Layout.preferredWidth: Type.chipHeight
                                Layout.fillHeight: true
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: Type.iconSize
                                    height: 2
                                    color: { plot.traces ? plot.traces.appearanceRevision : 0; return view.curveColor(measureRow.path) }
                                }
                                TapHandler { onTapped: curveSettings.openFor(measureRow.path) }
                            }
                            // Its name, then how it is drawn when not as read: the
                            // table gives readings while the line is drawn scaled.
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: Type.tight
                                Text {
                                    objectName: "plotMeasureName"
                                    Layout.fillWidth: true
                                    Layout.maximumWidth: implicitWidth
                                    text: measureRow.path
                                    textFormat: Text.PlainText
                                    elide: Text.ElideMiddle
                                    color: Theme.textPrimary
                                    font.pointSize: Type.caption
                                    TapHandler { onTapped: view.focusedPath = view.focusedPath === measureRow.path ? "" : measureRow.path }
                                }
                                Rectangle {
                                    objectName: "plotMeasureTransform"
                                    visible: measureRow.drawnAs.length > 0
                                    Layout.preferredWidth: measureTag.implicitWidth + 2 * Type.tight
                                    Layout.preferredHeight: Type.chipHeight - 2
                                    radius: Type.radius
                                    color: "transparent"
                                    border.color: Theme.warning
                                    Text {
                                        id: measureTag
                                        anchors.centerIn: parent
                                        textFormat: Text.PlainText
                                        font.family: Type.fixedFamily
                                        font.pointSize: Type.caption
                                        color: Theme.warning
                                        text: measureRow.drawnAs
                                    }
                                }
                                Item { Layout.fillWidth: true }
                            }
                            Repeater {
                                model: measureTable.keys
                                delegate: Text {
                                    id: statCell
                                    required property string modelData
                                    // Min's and Max's reading time: a click puts the
                                    // cursor there.
                                    readonly property var at: modelData === "min" ? measureRow.stats.minUtcMs
                                                            : modelData === "max" ? measureRow.stats.maxUtcMs : undefined
                                    objectName: "plotMeasure_" + modelData
                                    // A state shows its reading and an empty Δ; its
                                    // shares span the rest.
                                    visible: !measureRow.states || modelData === "now" || modelData === "delta"
                                    Layout.preferredWidth: measureTable.cell
                                    horizontalAlignment: Text.AlignRight
                                    text: measureRow.cells[modelData]
                                    textFormat: Text.PlainText
                                    elide: measureRow.states ? Text.ElideRight : Text.ElideNone
                                    font.family: Type.fixedFamily
                                    font.pointSize: Type.caption
                                    font.underline: at !== undefined && extremeHover.hovered
                                    color: at !== undefined && extremeHover.hovered ? Theme.accent : Theme.textPrimary
                                    HoverHandler {
                                        id: extremeHover
                                        enabled: statCell.at !== undefined
                                        cursorShape: Qt.PointingHandCursor
                                        onHoveredChanged: measureRow.extremeHovers += hovered ? 1 : -1
                                    }
                                    TapHandler {
                                        enabled: statCell.at !== undefined
                                        onTapped: view.putCursorAt(statCell.at)
                                    }
                                    PlainToolTip {
                                        visible: extremeHover.hovered
                                        delay: 700
                                        text: statCell.at === undefined ? ""
                                            : qsTr("Put the cursor on this reading, %1").arg(plot.clockText(statCell.at))
                                    }
                                }
                            }
                            // A state: its share of the time and its changes, over
                            // the figures after its reading.
                            Text {
                                objectName: "plotMeasureShares"
                                visible: measureRow.states
                                Layout.preferredWidth: measureTable.cell * measureTable.sharesCells
                                // Apart from the state's word when no Δ is between.
                                leftPadding: Type.spacing
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
                                text: plot.sharesText(measureRow.stats)
                                color: Theme.textPrimary
                                font.pointSize: Type.caption
                            }
                            ToolButton {
                                objectName: "plotMeasureRemove"
                                Layout.leftMargin: Type.spacing
                                Layout.preferredWidth: Type.chipHeight
                                Layout.preferredHeight: Type.rowHeight
                                padding: 0
                                flat: true
                                focusPolicy: Qt.TabFocus
                                text: qsTr("×")
                                Accessible.name: plot.recording ? qsTr("Remove %1 from the Trend").arg(measureRow.path)
                                                                : qsTr("Remove %1 from the Plot").arg(measureRow.path)
                                onClicked: plot.traces.unplot(measureRow.path)
                            }
                        }
                        HoverHandler { id: rowHover }
                        PlainToolTip {
                            objectName: "plotMeasureRowTip"
                            visible: rowHover.hovered && measureRow.hidden.length > 0 && measureRow.extremeHovers === 0
                            delay: 700
                            text: measureRow.hidden
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: chipMenu.openFor(measureRow.path)
                        }
                        Accessible.role: Accessible.Row
                        Accessible.name: measureRow.states ? qsTr("%1, %2").arg(measureRow.path).arg(plot.sharesText(measureRow.stats))
                            : qsTr("%1: minimum %2, maximum %3, mean %4, peak to peak %5, standard deviation %6")
                                .arg(measureRow.path).arg(measureRow.number(measureRow.stats.min)).arg(measureRow.number(measureRow.stats.max))
                                .arg(measureRow.number(measureRow.stats.mean)).arg(measureRow.number(measureRow.stats.peakToPeak))
                                .arg(measureRow.number(measureRow.stats.deviation))
                    }
                }
            }
            // A chip per plotted value: its colour, its path, its newest reading,
            // and × to take it off the Plot (its readings are kept). Up to three
            // rows of them, then they scroll, so the graph keeps its room.
            Flickable {
                id: legendScroll
                objectName: "plotLegendScroll"
                visible: plot.plotted.length > 0 && !plot.measuring
                Layout.fillWidth: true
                Layout.leftMargin: Type.padding
                Layout.rightMargin: Type.padding
                Layout.preferredHeight: Math.min(legend.implicitHeight, plot.oneChipRow ? Type.controlHeight
                                                                                        : 3 * Type.controlHeight + 2 * Type.spacing)
                readonly property bool scrolls: legend.implicitHeight > height + 0.5
                contentWidth: width
                contentHeight: legend.implicitHeight
                clip: true
                interactive: scrolls
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    id: legendBar
                    objectName: "plotLegendScrollBar"
                    policy: ScrollBar.AsNeeded
                }
                Flow {
                    id: legend
                    objectName: "plotLegend"
                    width: legendScroll.width - (legendScroll.scrolls ? legendBar.width : 0)
                    spacing: Type.spacing
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
                                view.rightUtcMs
                                if (!plot.traces)
                                    return undefined
                                return plot.cursorInView ? view.valueAt(path)
                                     : plot.recording ? view.valueAtRight(path) : plot.traces.latest(path)
                            }
                            // Taken out of the Watchlist: its readings stay, none come.
                            readonly property bool gone: {
                                plot.shownTick
                                // A recorded run's values are all there is: never "not read".
                                return !plot.recording && plot.traces !== null && !plot.traces.watched(path)
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
                                if (!view.referenceShown || words.length > 0 || (plot.traces && plot.traces.categorical(path)))
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
                                    Accessible.name: plot.recording ? qsTr("Remove %1 from the Trend").arg(chip.path)
                                                                    : qsTr("Remove %1 from the Plot").arg(chip.path)
                                    Accessible.description: plot.recording ? qsTr("The recording is unchanged.") : qsTr("Its readings are kept.")
                                    ToolTip.visible: hovered || visualFocus
                                    ToolTip.text: plot.recording ? qsTr("Remove from the Trend; the recording is unchanged")
                                                                 : qsTr("Remove from the Plot; its readings are kept")
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
                            Accessible.name: plot.recording
                                             ? qsTr("%1, %2 at %3").arg(chip.path).arg(chip.shown)
                                                   .arg(plot.axisWords(plot.cursorInView ? view.cursorUtcMs : view.rightUtcMs))
                                             : plot.cursorInView ? qsTr("%1, %2 at %3").arg(chip.path).arg(chip.shown).arg(plot.clockText(view.cursorUtcMs))
                                                                 : qsTr("%1, %2, latest reading").arg(chip.path).arg(chip.shown)
                        }
                    }
                }
            }

            Item {
                objectName: "plotGraph"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: plot.graphMinimumHeight
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
                    // The box's size on a chip, as the cursor's time is.
                    chipColor: Theme.surface
                    chipHeight: Type.chipHeight
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    visible: plot.plotted.length > 0 && !plot.waiting
                    // The graph's menu from the keyboard.
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_M && event.modifiers === Qt.NoModifier) {
                            plot.measuring = !plot.measuring
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_L && event.modifiers === Qt.NoModifier) {
                            plot.toggleStack()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)) {
                            if (event.modifiers & Qt.ShiftModifier)
                                plot.copyValues()
                            else
                                plot.copyPicture()
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                            graphMenu.at = -1
                            graphMenu.popup(view, view.plotArea.x + Type.padding, view.plotArea.y + Type.padding)
                            event.accepted = true
                        }
                    }
                    Accessible.role: Accessible.Graphic
                    Accessible.name: {
                        if (plot.recording) {
                            if (plot.runName.length === 0)
                                return plot.plotted.length === 1 ? qsTr("Trend of 1 value") : qsTr("Trend of %1 values").arg(plot.plotted.length)
                            return plot.plotted.length === 1 ? qsTr("Trend of 1 value, run of %1").arg(plot.runName)
                                                             : qsTr("Trend of %1 values, run of %2").arg(plot.plotted.length).arg(plot.runName)
                        }
                        const state = view.following ? qsTr("following")
                                                      : qsTr("%1 behind").arg(plot.behindWords(view.behindSeconds))
                        return plot.plotted.length === 1 ? qsTr("Plot of 1 value, %1").arg(state)
                                                         : qsTr("Plot of %1 values, %2").arg(plot.plotted.length).arg(state)
                    }
                    Accessible.description: qsTr("Arrows pan, plus and minus zoom, Shift+arrows step the cursor, Enter pins a reference, Home goes to the oldest reading, End follows, 1 to 4 choose the span; Up and Down move the values, Control with them zooms the values, A fits them again; L stacks the numbers in lanes of their own.")
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
                // A gap band too narrow for its caption says it on hover.
                PlainToolTip {
                    objectName: "plotGapTip"
                    visible: view.hoverGap.length > 0
                    delay: 300
                    x: view.hoverGapX + Type.padding
                    y: view.plotArea.y + Type.chipHeight
                    text: view.hoverGap
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
                    x: view.numberLanes.length > 0 ? view.numberLanes[0].nameRect.right + Type.padding
                                                   : view.valuesArea.left + Type.padding
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
                    visible: view.visible && view.valueAuto && view.focusedPath.length > 0 && view.numberLanes.length === 0
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
                    // Merged into the end's chip while the two would collide.
                    visible: view.visible && view.referenceShown && plot.onPlot(view.referenceX) && !endChip.merged
                    width: referenceText.implicitWidth + 2 * Type.padding
                    height: Type.chipHeight
                    // Under its line, beside the axis switch if it would cover it.
                    x: plot.chipX(view.referenceX, width, parent.width, [timeAxis])
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
                // The end's time under the axis: how long after the reference,
                // then the time of day, beside the reference's chip.
                Rectangle {
                    id: endChip
                    objectName: "plotEndChip"
                    visible: view.visible && view.endShown && plot.onPlot(view.endX)
                    width: endText.implicitWidth + 2 * Type.padding
                    height: Type.chipHeight
                    // Both pins crowd the axis: the lines are closer than their
                    // two chips are wide, or the two chips and the axis switch
                    // do not fit side by side. Then this chip stands for both
                    // ("Δ 0.4 s · 14:14:17.875") and the reference's is not shown.
                    readonly property bool merged: view.visible && view.referenceShown && view.endShown
                        && plot.onPlot(view.referenceX) && plot.onPlot(view.endX)
                        && (Math.abs(view.endX - view.referenceX) < (referenceChip.width + width) / 2 + 8
                            || referenceChip.width + width + timeAxis.width + 3 * 4 + Type.tight > parent.width)
                    x: merged ? plot.chipX((view.endX + view.referenceX) / 2, width, parent.width, [timeAxis])
                              : plot.chipX(view.endX, width, parent.width, [referenceChip, timeAxis])
                    y: view.height - height
                    radius: Type.radius
                    color: Theme.infoContainer
                    border.color: Theme.info
                    readonly property real afterMs: view.endUtcMs - view.referenceUtcMs
                    Text {
                        id: endText
                        objectName: "plotEndText"
                        anchors.centerIn: parent
                        textFormat: Text.PlainText
                        font.family: Type.fixedFamily
                        font.pointSize: Type.caption
                        color: Theme.textPrimary
                        text: qsTr("Δ %1 · %2").arg(plot.spanText(endChip.afterMs)).arg(plot.clockText(view.endUtcMs))
                    }
                    Accessible.role: Accessible.StaticText
                    Accessible.name: endChip.afterMs >= 0
                                     ? qsTr("End at %1, %2 after the reference").arg(plot.clockText(view.endUtcMs))
                                           .arg(plot.spanText(endChip.afterMs))
                                     : qsTr("End at %1, %2 before the reference").arg(plot.clockText(view.endUtcMs))
                                           .arg(plot.spanText(-endChip.afterMs))
                    Accessible.description: qsTr("Escape removes it.")
                }
                // The cursor's time under the axis: how long ago while following,
                // the time since the reference once one is pinned.
                Rectangle {
                    objectName: "plotCursorChip"
                    // On the end, the end's chip says it all.
                    visible: view.visible && view.cursorShown && plot.onPlot(view.cursorX)
                             && !(endChip.visible && view.cursorUtcMs === view.endUtcMs)
                    width: cursorText.implicitWidth + 2 * Type.padding
                    height: Type.chipHeight
                    // Centred on its line, but never over the reference's or
                    // the end's chip or the axis switch: beside them, 4 px apart.
                    x: plot.chipX(view.cursorX, width, parent.width, [referenceChip, endChip, timeAxis])
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
                            // From the start: the time since it, then the time
                            // of day; a recorded run on the time of day says both too.
                            const since = plot.sinceText(view.cursorUtcMs - view.originUtcMs)
                            const at = view.fromStart ? qsTr("%1 · %2").arg(since).arg(plot.clockText(view.cursorUtcMs))
                                     : plot.recording ? qsTr("%1 · +%2").arg(plot.clockText(view.cursorUtcMs)).arg(since)
                                                      : plot.clockText(view.cursorUtcMs)
                            if (view.referenceShown)
                                return qsTr("Δ %1 · %2").arg(plot.spanText(view.cursorUtcMs - view.referenceUtcMs)).arg(at)
                            if (view.following && plot.followable)
                                return qsTr("%1 · %2 ago").arg(at).arg(plot.agoText(view.cursorAgoSeconds))
                            return at
                        }
                    }
                    Accessible.role: Accessible.StaticText
                    Accessible.name: view.referenceShown
                                     ? qsTr("Cursor at %1, %2 from the reference").arg(plot.clockText(view.cursorUtcMs))
                                           .arg(plot.spanText(view.cursorUtcMs - view.referenceUtcMs))
                                     : plot.recording ? qsTr("Cursor at %1 seconds from run start, %2")
                                                            .arg(((view.cursorUtcMs - view.originUtcMs) / 1000).toFixed(3))
                                                            .arg(plot.clockText(view.cursorUtcMs))
                                     : view.following ? qsTr("Cursor at %1, %2 seconds ago").arg(plot.clockText(view.cursorUtcMs))
                                                            .arg(view.cursorAgoSeconds.toFixed(1))
                                                      : qsTr("Cursor at %1").arg(plot.clockText(view.cursorUtcMs))
                }
                // Held: how far behind the newest reading, and a way back to it.
                Basic.Button {
                    id: behind
                    objectName: "plotBehind"
                    visible: view.visible && !view.following && plot.followable
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
                    // The last to give way: "Clock ▾" and "Start ▾" when the
                    // Plot is at its narrowest; its accessible name stays.
                    readonly property bool brief: plot.narrowStep >= 4
                    text: (view.fromStart ? (brief ? qsTr("Start") : plot.recording ? qsTr("From run start (s)") : qsTr("From start"))
                                          : (brief ? qsTr("Clock") : qsTr("Time of day"))) + " ▾"
                    FontMetrics { id: axisMetrics; font: timeAxis.font }
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
                    Accessible.name: !view.fromStart ? qsTr("Time axis: time of day")
                                   : plot.recording ? qsTr("Time axis: from run start") : qsTr("Time axis: from the start of reading")
                    ToolTip.visible: hovered || visualFocus
                    ToolTip.delay: 700
                    ToolTip.text: !view.fromStart ? qsTr("Time this PC received each reading (local time, %1)").arg(plot.utcOffset())
                                : plot.recording ? qsTr("Time since the run started, at %1 on this PC's clock").arg(plot.clockText(view.originUtcMs))
                                                 : qsTr("Time since the Watchlist started reading, at %1 on this PC's clock").arg(plot.clockText(view.originUtcMs))
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
                            text: plot.recording ? qsTr("From run start") : qsTr("From the start of reading")
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
                    text: plot.waitingText
                }
                // The host's word over the graph: "No readings here. Press All…"
                Label {
                    objectName: "plotMessage"
                    visible: view.visible && plot.message.length > 0
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Type.padding, 360)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    text: plot.message
                    padding: Type.tight
                    background: Rectangle { color: Theme.surfaceWindow; opacity: 0.85; radius: Type.radius }
                    Accessible.role: Accessible.StaticText
                    Accessible.name: plot.message
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
                        text: plot.emptyText
                    }
                    // Rows have no grip: here a user learns that dragging works.
                    Label {
                        objectName: "plotEmptyHint"
                        Layout.fillWidth: true
                        visible: plot.traces !== null && plot.emptyHint.length > 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        font.pointSize: Type.caption
                        text: plot.emptyHint
                    }
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
            visible: plot.followable
            height: visible ? implicitHeight : 0
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
        MenuSeparator {}
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
            objectName: "plotMenuPinEnd"
            text: qsTr("Pin end here") + "\t" + qsTr("Shift+Enter")
            // Without a reference it pins one first, as Shift and a click do.
            enabled: plot.plotted.length > 0
            onTriggered: {
                if (graphMenu.at >= view.plotArea.left)
                    view.setCursorAt(graphMenu.at)
                else if (!view.cursorShown)
                    view.stepCursor(0)
                view.pinEnd()
            }
        }
        MenuItem {
            objectName: "plotMenuUnpinEnd"
            text: qsTr("Remove end")
            enabled: view.endShown
            onTriggered: view.clearEnd()
        }
        MenuItem {
            objectName: "plotMenuUnpin"
            text: qsTr("Remove reference") + "\t" + qsTr("Esc")
            enabled: view.referenceShown
            onTriggered: view.clearReference()
        }
        MenuSeparator {}
        MenuItem {
            objectName: "plotMenuMeasure"
            text: qsTr("Measure") + "\t" + qsTr("M")
            checkable: true
            checked: plot.measuring
            onTriggered: {
                plot.measuring = !plot.measuring
                checked = Qt.binding(() => plot.measuring)
            }
        }
        MenuItem {
            objectName: "plotMenuStack"
            text: qsTr("Stack numbers in lanes") + "\t" + qsTr("L")
            checkable: true
            checked: plot.traces !== null && plot.traces.stacked
            // Too many numbers for this height: L says why.
            enabled: plot.traces !== null && (plot.traces.stacked || view.stackFits)
            onTriggered: {
                plot.toggleStack()
                checked = Qt.binding(() => plot.traces !== null && plot.traces.stacked)
            }
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
            objectName: "plotMenuNextChange"
            text: qsTr("Next change") + "\t" + qsTr("Ctrl+Right")
            onTriggered: view.stepChange(1)
        }
        MenuItem {
            objectName: "plotMenuPreviousChange"
            text: qsTr("Previous change") + "\t" + qsTr("Ctrl+Left")
            onTriggered: view.stepChange(-1)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "plotMenuCopyPicture"
            text: qsTr("Copy picture") + "\t" + qsTr("Ctrl+C")
            enabled: plot.plotted.length > 0
            onTriggered: plot.copyPicture()
        }
        MenuItem {
            objectName: "plotMenuCopyValues"
            text: qsTr("Copy values") + "\t" + qsTr("Ctrl+Shift+C")
            enabled: plot.plotted.length > 0
            onTriggered: plot.copyValues()
        }
        MenuItem {
            objectName: "plotMenuSaveValues"
            text: qsTr("Save values as CSV…")
            enabled: plot.plotted.length > 0
            onTriggered: {
                saveDialog.selectedFile = saveDialog.currentFolder + "/" + plot.csvName()
                saveDialog.open()
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "plotMenuRemoveAll"
            text: qsTr("Remove all curves")
            enabled: plot.plotted.length > 0
            // One step to undo; their readings are kept, as with each chip's ×.
            onTriggered: plot.traces.unplotMany(plot.plotted.slice())
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
            objectName: "plotChipMinimum"
            text: qsTr("Go to minimum")
            enabled: plot.traces !== null && !plot.traces.categorical(chipMenu.path)
            onTriggered: {
                const stats = view.statistics(chipMenu.path)
                if (stats.minUtcMs !== undefined)
                    view.putCursorAt(stats.minUtcMs)
            }
        }
        MenuItem {
            objectName: "plotChipMaximum"
            text: qsTr("Go to maximum")
            enabled: plot.traces !== null && !plot.traces.categorical(chipMenu.path)
            onTriggered: {
                const stats = view.statistics(chipMenu.path)
                if (stats.maxUtcMs !== undefined)
                    view.putCursorAt(stats.maxUtcMs)
            }
        }
        MenuSeparator {}
        MenuItem {
            objectName: "plotChipSettingsItem"
            text: qsTr("Curve settings…")
            onTriggered: curveSettings.openFor(chipMenu.path)
        }
        MenuItem {
            objectName: "plotChipRemoveItem"
            text: plot.recording ? qsTr("Remove from the Trend") : qsTr("Remove from the Plot")
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
                text: qsTr("The readings stay as they were read; the chip and the cursor give them. All plotted numbers share one value axis unless they are stacked in lanes (L).")
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

    // Rows dragged from the Watchlist or the Device tree (LP-1g3): a frame and
    // what a drop will do.
    DropArea {
        id: plotDrop
        objectName: "plotDrop"
        // A recorded run draws what was recorded: nothing is dropped on it.
        enabled: !plot.recording
        anchors.fill: parent
        keys: ["application/x-telemetry-paths"]
        property string words: ""
        onEntered: (drag) => {
            let payload = null
            try { payload = JSON.parse(drag.getDataAsString("application/x-telemetry-paths")) } catch (e) {}
            const paths = payload && Array.isArray(payload.paths) ? payload.paths : []
            if (!plot.traces || paths.length === 0) {
                drag.accepted = false
                return
            }
            words = plot.dropWords(payload)
        }
        onExited: words = ""
        onDropped: (drop) => {
            let payload = null
            try { payload = JSON.parse(drop.getDataAsString("application/x-telemetry-paths")) } catch (e) {}
            words = ""
            if (plot.dropPaths(payload))
                drop.acceptProposedAction()
        }
        Rectangle {
            anchors.fill: parent
            visible: plotDrop.containsDrag
            color: "transparent"
            border.width: 2
            border.color: Theme.focusRing
            Label {
                anchors.centerIn: parent
                text: plotDrop.words
                color: Theme.textPrimary
                padding: Type.padding
                background: Rectangle { color: Theme.surface; radius: Type.radius; border.color: Theme.outline }
            }
        }
    }
}
