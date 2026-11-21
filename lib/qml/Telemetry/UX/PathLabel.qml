// One path label for every panel that shows a path (the designer's review of
// 20 November 2026, 14.3): the Watchlist's List, Details' title, the Device
// tree's context line. A path reads group/…/endpoint.member[i]; its prefix
// and separators are muted, its leaf bright. When room is short whole
// segments drop under one "…", in this order: the middle groups nearest the
// branch, then the branch, then the remaining group, then the endpoint; the
// nearest parent lasts longest, since it tells twins apart (phaseA/H3 from
// phaseB/H3). Only then does the leaf elide in its middle. A screen reader
// hears the leaf first ("H3, in metrology, harmonics, phaseA"); the tooltip
// holds the whole path, broken after each "/". The label stays left to right
// in a mirrored layout: a path is code, not prose.
//
// interactive: the prefix's groups are links, one Tab stop for the whole
// label; Left and Right move between them, Enter or a click activates one
// (segmentActivated with that group's path).
//
// One measure decides and lays out (PL-1): every piece is placed at the
// advance width its font gives it, the same number that decides the drops,
// and a piece within 1 px of its room counts as fitting. A Text's own width
// is not used: it counts a glyph's overhang past its advance ("/" in Segoe
// UI), so a label sized to its implicit width once cut its leaf by a
// fraction of a pixel ("measure/P…r"). MIT.
import QtQuick
import QtQuick.Controls

Item {
    id: label
    objectName: "pathLabel"
    property string path: ""
    property font font: Qt.application.font
    property int leafWeight: Font.Normal
    property color prefixColor: Theme.textMuted
    property color leafColor: Theme.textPrimary
    property bool interactive: false
    // An interactive label is one Tab stop of its own; false leaves the
    // keyboard to its host (the Device tree's context line: Left reaches it).
    property bool tabStop: true
    // The leaf is a link too (every segment of the context line is a group).
    property bool leafLink: false
    // The whole path in the tooltip on hover, where something was cut.
    property bool toolTipEnabled: true
    // Words after the path in the tooltip (a row's kind and type).
    property string toolTipDetail: ""
    // The names a host's checks find the two texts by.
    property string prefixObjectName: "pathLabelPrefix"
    property string leafObjectName: "pathLabelLeaf"
    // Where the text ends, and the leaf's own width uncut.
    readonly property real usedWidth: leafText.x + leafText.width
    readonly property real leafImplicitWidth: leafText.implicitWidth
    // The narrowest the label gets with its leaf whole: every segment that
    // can drop gone ("…/SetEnabled"), by the measure that decides (it fits a
    // room within the tolerance). A host gives way to the label below it.
    readonly property real shortestWidth: {
        void label.font
        void label.leafFont
        return measure(shape(dropOrder.length))
    }
    // A piece within this of its room fits: two measures of one text differ
    // by a fraction of a pixel, and that must never cut a name.
    readonly property real tolerance: 1
    signal segmentActivated(string groupPath)

    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true
    implicitHeight: Math.ceil(Math.max(prefixMetrics.height, leafMetrics.height))
    implicitWidth: {
        void label.font
        void label.leafFont
        return Math.ceil(measure(full))
    }
    // The label's font with another weight or an underline.
    function variant(weight, underline) {
        const spec = { family: font.family, weight: weight, italic: font.italic, underline: underline }
        if (font.pixelSize > 0)
            spec.pixelSize = font.pixelSize
        else
            spec.pointSize = font.pointSize
        return Qt.font(spec)
    }
    readonly property font leafFont: variant(leafWeight, false)
    readonly property font linkFont: variant(font.weight, true)
    FontMetrics { id: prefixMetrics; font: label.font }
    FontMetrics { id: leafMetrics; font: label.leafFont }

    // The path's steps: {text, sep (the separator before it), kind ("group",
    // "endpoint", "member" or "element"), groupPath (a group's own path)}.
    // A backslash keeps the character after it in its step.
    readonly property var steps: {
        const out = []
        const text = path
        let slash = -1
        for (let i = 0; i < text.length; ++i) {
            if (text[i] === "\\") { ++i; continue }
            if (text[i] === "/") slash = i
        }
        let start = 0, sep = ""
        const push = function(end, kind, nextSep) {
            if (end > start || kind === "endpoint")
                out.push({ text: text.substring(start, end), sep: sep, kind: kind,
                           groupPath: kind === "group" ? text.substring(0, end) : "" })
            sep = nextSep
        }
        // The groups, each up to its "/".
        for (let i = 0; i <= slash; ++i) {
            if (text[i] === "\\") { ++i; continue }
            if (text[i] === "/") { push(i, "group", "/"); start = i + 1 }
        }
        // The endpoint, then its members and elements.
        let kind = "endpoint"
        for (let i = start; i < text.length; ++i) {
            const c = text[i]
            if (c === "\\") { ++i; continue }
            if (c === ".") { push(i, kind, "."); start = i + 1; kind = "member" }
            else if (c === "[") { push(i, kind, ""); start = i; kind = "element" }
            else if (c === "]" && kind === "element") { push(i + 1, kind, ""); start = i + 1; kind = "member" }
        }
        if (start < text.length)
            out.push({ text: text.substring(start), sep: sep, kind: kind, groupPath: "" })
        return out
    }
    // Which steps drop, first to last: the middle groups nearest the branch
    // first, the steps between the endpoint and the nearest parent, the
    // branch, the endpoint, and the nearest parent last.
    readonly property var dropOrder: {
        const n = steps.length
        const parent = n - 2
        if (parent < 0)
            return []
        let endpoint = steps.findIndex(function(step) { return step.kind !== "group" })
        if (endpoint < 0) endpoint = n - 1
        const order = []
        for (let i = 1; i < Math.min(endpoint, parent); ++i)
            order.push(i)
        for (let i = endpoint + 1; i < parent; ++i)
            order.push(i)
        if (parent > 0)
            order.push(0)
        if (endpoint > 0 && endpoint < parent)
            order.push(endpoint)
        order.push(parent)
        return order
    }
    // The label with the first `drops` of dropOrder gone: {parts, prefix, leaf}.
    function shape(drops) {
        const n = steps.length
        if (n === 0)
            return { parts: [], prefix: "", leaf: "" }
        const gone = {}
        for (let k = 0; k < drops && k < dropOrder.length; ++k)
            gone[dropOrder[k]] = true
        const parts = []
        let previousGone = false
        for (let i = 0; i < n - 1; ++i) {
            const step = steps[i]
            if (gone[i]) {
                if (!previousGone)
                    parts.push({ text: "…", sep: step.sep, kind: "cut", groupPath: "" })
                previousGone = true
                continue
            }
            previousGone = false
            parts.push(step)
        }
        const leafStep = steps[n - 1]
        const prefix = parts.map(function(part) { return part.sep + part.text }).join("") + leafStep.sep
        return { parts: parts, prefix: prefix, leaf: leafStep.text }
    }
    readonly property var full: shape(0)
    // Where a shape's leaf starts: its prefix's advance, piece by piece as
    // the links lay it out, else in one piece as the prefix's Text.
    function prefixAdvance(s) {
        if (!interactive)
            return prefixMetrics.advanceWidth(s.prefix)
        let x = 0
        for (const part of s.parts)
            x += prefixMetrics.advanceWidth(part.sep + part.text)
        return steps.length > 0 ? x + prefixMetrics.advanceWidth(steps[steps.length - 1].sep) : x
    }
    // A shape's whole width, as the label lays it out.
    function measure(s) {
        return prefixAdvance(s) + leafMetrics.advanceWidth(s.leaf)
    }
    // The fewest drops that fit the width; all of them when nothing fits,
    // and then the leaf elides.
    readonly property int drops: {
        void label.font
        void label.leafFont
        const room = width
        for (let k = 0; k <= dropOrder.length; ++k)
            if (measure(shape(k)) <= room + tolerance)
                return k
        return dropOrder.length
    }
    readonly property var shown: shape(drops)
    // The shown leaf's place and whether it is whole, by the same measure.
    readonly property real leafX: {
        void label.font
        return prefixAdvance(shown)
    }
    readonly property bool leafFits: {
        void label.leafFont
        return leafX + leafMetrics.advanceWidth(shown.leaf) <= width + tolerance
    }
    readonly property bool elided: drops > 0 || !leafFits
    // The groups above the leaf, for the leaf-first spoken name.
    readonly property var parentNames: steps.slice(0, Math.max(0, steps.length - 1)).map(function(step) {
        return step.text.replace(/\\(.)/g, "$1")
    })
    readonly property string leafName: steps.length > 0 ? steps[steps.length - 1].text.replace(/\\(.)/g, "$1") : ""
    readonly property string spokenName: parentNames.length === 0 ? leafName
        : qsTr("%1, in %2").arg(leafName).arg(parentNames.join(", "))

    // The keyboard's segment among the links (interactive), or -1.
    property int currentLink: -1
    readonly property var links: shown.parts.filter(function(part) { return part.kind === "group" })
    function activate(index) {
        if (index >= 0 && index < links.length)
            label.segmentActivated(links[index].groupPath)
    }
    // Kept while it has the keys: narrowed until its last link drops, the
    // label still holds them (a Tab stop is not taken from under the focus).
    activeFocusOnTab: tabStop && interactive && (links.length > 0 || activeFocus)
    onActiveFocusChanged: if (activeFocus && currentLink < 0 && links.length > 0) currentLink = links.length - 1
    onLinksChanged: if (currentLink >= links.length) currentLink = links.length - 1
    Keys.enabled: interactive
    Keys.onLeftPressed: function(event) { if (currentLink > 0) --currentLink; event.accepted = true }
    Keys.onRightPressed: function(event) { if (currentLink < links.length - 1) ++currentLink; event.accepted = true }
    Keys.onReturnPressed: function(event) { activate(currentLink); event.accepted = true }
    Keys.onEnterPressed: function(event) { activate(currentLink); event.accepted = true }
    Accessible.role: interactive ? Accessible.Link : Accessible.StaticText
    Accessible.name: spokenName
    Accessible.description: interactive && links.length > 0
        ? qsTr("Left and Right move between the groups; Enter shows one in the Device tree.") : ""
    Accessible.focusable: interactive

    // Each piece is as wide as its advance; a glyph's overhang is drawn past it.
    Text {
        id: prefixText
        objectName: label.prefixObjectName
        visible: !label.interactive && text.length > 0
        anchors.verticalCenter: parent.verticalCenter
        width: label.leafX
        textFormat: Text.PlainText
        text: label.shown.prefix
        font: label.font
        color: label.prefixColor
    }
    Row {
        id: linkRow
        objectName: "pathLabelLinks"
        visible: label.interactive
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            model: label.interactive ? label.shown.parts : []
            Text {
                id: part
                required property var modelData
                required property int index
                readonly property bool link: modelData.kind === "group"
                // Its place among the links: the groups before it in the parts.
                readonly property int linkIndex: !link ? -1 : label.shown.parts.slice(0, index).filter(function(p) { return p.kind === "group" }).length
                objectName: link ? "pathLabelLink" : "pathLabelPart"
                width: prefixMetrics.advanceWidth(text)
                textFormat: Text.PlainText
                text: modelData.sep + modelData.text
                font: link && (partHover.hovered || (label.activeFocus && linkIndex === label.currentLink))
                      ? label.linkFont : label.font
                color: label.prefixColor
                HoverHandler { id: partHover; enabled: part.link; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: part.link; onTapped: label.segmentActivated(part.modelData.groupPath) }
                Rectangle {
                    objectName: "pathLabelLinkFocus"
                    visible: part.link && label.activeFocus && part.linkIndex === label.currentLink
                    x: part.modelData.sep.length > 0 ? prefixMetrics.advanceWidth(part.modelData.sep) - 2 : -2
                    y: -1
                    width: parent.width - x + 2
                    height: parent.height + 2
                    radius: 3
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.focusRing
                }
            }
        }
        Text {
            visible: label.interactive && text.length > 0
            width: prefixMetrics.advanceWidth(text)
            textFormat: Text.PlainText
            text: label.steps.length > 0 ? label.steps[label.steps.length - 1].sep : ""
            font: label.font
            color: label.prefixColor
        }
    }
    // Whole while it fits by the measure; only when nothing more can drop
    // does it elide in its middle, in the room left.
    Text {
        id: leafText
        objectName: label.leafObjectName
        x: label.leafX
        anchors.verticalCenter: parent.verticalCenter
        width: label.leafFits ? implicitWidth : Math.max(0, label.width - x)
        textFormat: Text.PlainText
        text: label.shown.leaf
        font: label.interactive && label.leafLink && leafHover.hovered ? label.variant(label.leafWeight, true) : label.leafFont
        color: label.leafColor
        elide: label.leafFits ? Text.ElideNone : Text.ElideMiddle
        HoverHandler { id: leafHover; enabled: label.interactive && label.leafLink; cursorShape: Qt.PointingHandCursor }
        TapHandler { enabled: label.interactive && label.leafLink; onTapped: label.segmentActivated(label.path) }
    }
    HoverHandler { id: labelHover; enabled: label.toolTipEnabled }
    PlainToolTip {
        objectName: "pathLabelToolTip"
        visible: label.toolTipEnabled && labelHover.hovered && (label.elided || label.toolTipDetail.length > 0)
        // Broken after each "/", so a long path wraps between its groups.
        text: label.path.replace(/\//g, "/​") + (label.toolTipDetail.length > 0 ? "\n" + label.toolTipDetail : "")
        delay: 700
    }
}
