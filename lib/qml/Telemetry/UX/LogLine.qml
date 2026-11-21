// One line of the Log (LG-1, the designer's §7): its time; how bad, by a
// glyph for a warning or an error only (an info has none, and quieter
// words); where it came from - the kind word for an operation, else the
// subsystem; then what it acted on and what happened: the kind glyph, the
// one path label, what followed the path ("= 2", "(2, 3) → 6") and the
// outcome in words ("written · read back 2"). The words give way first, down
// to Type.nameFloor; then the path drops whole segments beside them. A
// host's delegate gives it a LogModel row's roles and says whether it is the
// current row: expanded, the words show whole (on the line when they fit,
// else wrapped under it) and a grouped event opens its list. Below 360 px
// (narrow) the source column goes; the word stays in the accessible name.
// It owns no model and no state. MIT.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: line
    objectName: "logLine"
    // A LogModel row's roles.
    property string timeText: ""
    property int severity: 0
    property string source: ""
    property string kind: ""
    property string path: ""
    property string values: ""
    property string words: ""
    property string detail: ""
    // The current row: the words whole and the list open.
    property bool expanded: false
    property bool narrow: false
    // On the selection fill the quieter words take the primary colour (muted
    // falls below 4.5:1 there); with focus every word the selection's own.
    property bool highlighted: false
    property bool focused: false
    // The columns' widths, one for every row: the host measures them once.
    property real timeWidth: Math.ceil(timeMetrics.advanceWidth) + 2
    property real sourceWidth: Math.ceil(sourceMetrics.advanceWidth) + 4

    readonly property string severityWord: severity >= 2 ? qsTr("Error") : severity === 1 ? qsTr("Warning")
                                                                                         : qsTr("Info")
    // What the row says aloud: the leaf first ("Reset, in control"), then
    // what followed it and the words; the list when it is open.
    readonly property string spoken: {
        const target = [pathLabel.spokenName, line.values].filter(function(part) { return part.length > 0 }).join(" ")
        const said = [line.timeText, line.severityWord, line.source, target, line.words]
            .filter(function(part) { return part.length > 0 }).join(", ")
        return line.expanded && line.detail.length > 0 ? said + ", " + line.detail.split("\n").join(", ") : said
    }
    readonly property color primaryColor: highlighted && focused ? Theme.selectedForeground : Theme.textPrimary
    readonly property color secondaryColor: highlighted ? primaryColor : Theme.textSecondary
    readonly property color mutedColor: highlighted ? primaryColor : Theme.textMuted
    // The words of an info are quieter; a warning's and an error's are not.
    readonly property color wordsColor: severity >= 1 ? primaryColor : secondaryColor
    // Where the words went: on the path's line, or under it (expanded, too long).
    readonly property bool wordsBelow: target.wordsBelow
    readonly property bool detailShown: expanded && detail.length > 0

    spacing: Type.padding
    TextMetrics { id: timeMetrics; font.family: Type.fixedFamily; font.pointSize: Type.caption; text: "00:00:00" }
    TextMetrics { id: sourceMetrics; font.pointSize: Type.caption; text: qsTr("Connection") }

    Label {
        objectName: "logLineTime"
        Layout.preferredWidth: line.timeWidth
        Layout.alignment: Qt.AlignTop
        Layout.preferredHeight: target.lineHeight
        verticalAlignment: Text.AlignVCenter
        font.family: Type.fixedFamily
        font.pointSize: Type.caption
        color: line.mutedColor
        text: line.timeText
    }
    // A warning's or an error's glyph; an info keeps the slot empty, so the
    // columns stay put.
    ToolIcon {
        objectName: "logLineSeverity"
        Layout.preferredWidth: Type.iconSize
        Layout.preferredHeight: Type.iconSize
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: Math.max(0, (target.lineHeight - Type.iconSize) / 2)
        name: line.severity >= 2 ? "error" : line.severity === 1 ? "warning" : ""
        color: line.severity >= 2 ? Theme.error : Theme.warning
        Accessible.ignored: true
    }
    Label {
        objectName: "logLineSource"
        visible: !line.narrow
        Layout.preferredWidth: line.sourceWidth
        Layout.alignment: Qt.AlignTop
        Layout.preferredHeight: target.lineHeight
        verticalAlignment: Text.AlignVCenter
        font.pointSize: Type.caption
        color: line.secondaryColor
        elide: Text.ElideRight
        text: line.source
    }
    // What it acted on, then what happened.
    Item {
        id: target
        objectName: "logLineTarget"
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.preferredHeight: implicitHeight

        readonly property real gap: Type.groupSpacing
        readonly property real lineHeight: Math.ceil(Math.max(Type.iconSize, metrics.height, pathLabel.implicitHeight))
        readonly property real kindRoom: line.kind.length > 0 ? Type.iconSize + Type.spacing : 0
        readonly property real valuesWidth: line.values.length > 0 ? Math.ceil(valuesMetrics.advanceWidth) : 0
        readonly property real wordsWidth: line.words.length > 0 ? Math.ceil(wordsMetrics.advanceWidth) : 0
        // After the path: the values, a gap, the words; their least room while
        // the path gives way beside them.
        readonly property real tailWidth: valuesWidth + (valuesWidth > 0 && wordsWidth > 0 ? gap : 0) + wordsWidth
        readonly property real tailFloor: Math.min(tailWidth, Type.nameFloor)
        readonly property real pathRoom: line.path.length === 0 ? 0
            : line.expanded || tailWidth === 0 ? Math.max(0, width - kindRoom)
            : Math.max(0, width - kindRoom - gap - tailFloor)
        readonly property real valuesX: kindRoom + (line.path.length > 0 ? pathLabel.usedWidth + gap : 0)
        readonly property real wordsX: valuesWidth > 0 ? valuesLabel.x + valuesLabel.width + gap : valuesX
        // Expanded words too long for the path's line go under it, whole.
        readonly property bool wordsBelow: line.expanded && line.words.length > 0
                                           && wordsX + wordsWidth > width + pathLabel.tolerance
        implicitHeight: lineHeight
            + (wordsBelow ? Type.tight + wordsLabel.implicitHeight : 0)
            + (line.detailShown ? Type.tight + detailLabel.implicitHeight : 0)

        FontMetrics { id: metrics; font: wordsLabel.font }
        TextMetrics { id: valuesMetrics; font: valuesLabel.font; text: line.values }
        TextMetrics { id: wordsMetrics; font: wordsLabel.font; text: line.words }

        KindIcon {
            objectName: "logLineKind"
            visible: line.kind.length > 0
            x: 0
            y: (target.lineHeight - height) / 2
            kind: line.kind
            color: line.secondaryColor
        }
        PathLabel {
            id: pathLabel
            objectName: "logLinePath"
            visible: line.path.length > 0
            x: target.kindRoom
            y: (target.lineHeight - height) / 2
            width: target.pathRoom
            height: implicitHeight
            path: line.path
            font: wordsLabel.font
            prefixColor: line.mutedColor
            leafColor: line.primaryColor
            prefixObjectName: "logLinePathPrefix"
            leafObjectName: "logLinePathLeaf"
        }
        Label {
            id: valuesLabel
            objectName: "logLineValues"
            visible: line.values.length > 0
            x: target.valuesX
            y: (target.lineHeight - height) / 2
            width: Math.max(0, Math.min(target.valuesWidth, target.width - x))
            // A path's values are code, not prose: left to right always.
            LayoutMirroring.enabled: false
            horizontalAlignment: Text.AlignLeft
            textFormat: Text.PlainText
            color: line.secondaryColor
            elide: Text.ElideRight
            text: line.values
        }
        Label {
            id: wordsLabel
            objectName: "logLineWords"
            visible: line.words.length > 0
            x: target.wordsBelow ? target.kindRoom : target.wordsX
            y: target.wordsBelow ? target.lineHeight + Type.tight : (target.lineHeight - height) / 2
            width: Math.max(0, target.width - x)
            textFormat: Text.PlainText
            color: line.wordsColor
            text: line.words
            elide: line.expanded ? Text.ElideNone : Text.ElideRight
            wrapMode: target.wordsBelow ? Text.Wrap : Text.NoWrap
            maximumLineCount: target.wordsBelow ? 8 : 1
        }
        // A grouped event's list: 20 paths, then "and 12 more".
        Label {
            id: detailLabel
            objectName: "logLineDetail"
            visible: line.detailShown
            x: target.kindRoom
            y: target.lineHeight + (target.wordsBelow ? Type.tight + wordsLabel.height : 0) + Type.tight
            width: Math.max(0, target.width - x)
            LayoutMirroring.enabled: false
            horizontalAlignment: Text.AlignLeft
            textFormat: Text.PlainText
            color: line.secondaryColor
            wrapMode: Text.Wrap
            text: line.detailShown ? line.detail : ""
        }
    }
}
