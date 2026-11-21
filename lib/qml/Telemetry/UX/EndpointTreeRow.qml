// One row of the Device tree: a watch box where the Watchlist can hold what
// the row stands for, a glyph for what it is, and the endpoint's or group's
// name, then its type or its count. The row is the type scale's row height;
// the box sits in a slot a row high that every row keeps, the glyph in the
// next, so every glyph and every name of one depth share an x. The current
// row is framed while the tree has the keyboard. MIT.
//
// The Basic style is imported here only, where the row draws its own box;
// the panel keeps the style the host chose. Colours come from the panel's
// palette, so a host that restyles the panel restyles its rows; a box that is
// off for now takes the Theme's disabled roles.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

TreeViewDelegate {
    id: treeRow
    objectName: "endpointRow"

    required property string name
    required property string detail
    required property bool isEndpoint

    // Optional schema/cache roles keep third-party providers compatible.
    property string typeText: ""
    // The type as a screen reader says it ("struct Config in adc, 3
    // members"); empty: worked out from typeText.
    property string typeSpoken: ""
    // The Type column shows every scope as "…::" (the panel's give-way
    // step 3); a row matched by its type still shows its own.
    property bool typeScopesCut: false
    property string accessText: ""
    property string accessMark: ""
    property string unit: ""
    property bool previewEligible: false
    property bool previewAggregate: false
    property bool previewChild: false
    property string previewValue: ""
    property string previewState: ""
    property real previewAgeMs: -1
    property string previewReason: ""
    property bool previewsShown: false
    property bool reserveWatchSpace: true
    property bool tableMode: false
    property real nameColumnWidth: 190
    property real valueColumnWidth: 150
    property real typeColumnWidth: 85
    property real accessColumnWidth: 70
    // The row family's columns, placed from the right edge (family_spec A2):
    // a width of 0 leaves a column out. rightClearance keeps the marks clear of
    // the vertical scroll bar.
    property real columnGap: Math.round(Type.bodyPixels * 0.9)
    property real rightClearance: 0
    // A group's count at the row's right edge, in words, as parts that give
    // way from the right when room is short ("7 Fields", "1 Command"), and
    // the whole of it for the tooltip and a screen reader, with the groups
    // under it ("7 Fields · 1 Command in 4 groups").
    property var countParts: []
    property string countFull: ""
    // The one indent step of the tree (the panel's, per descriptor and width).
    property real indentStep: Type.indent
    // What the row is (KindIcon): "group", "struct", "array", "value",
    // "command" or "service". Without one the row works it out.
    property string kind: ""
    // The row's category, 1 Field, 2 Command, 3 Service: what a row without a
    // kind is worked out from.
    property int category: 0
    // The row's path for its tooltip's first line ("device/Config"), and an
    // enum's names for its last.
    property string pathText: ""
    property string typeNames: ""
    // A find in progress: where the text matched this row, {in: "name" or
    // "type", start, length}, or empty. The run is marked: info, underlined.
    property var matchRun: ({})
    readonly property bool matched: matchRun && matchRun.length > 0
    // The current branch's guide (the tree's): its level and its rows.
    property int guideLevel: -1
    property int guideFrom: 0
    property int guideTo: -1
    // Why the box is off when the Watchlist cannot hold this row (yet).
    property string watchRefusal: ""
    // In the extended selection a drag and the drop keys act on (the
    // designer's section 16.2); drawn as the selection is.
    property bool picked: false
    // Measured texts, so the panel can size its columns to them: each label's
    // own width, reported again when the row is reused or the panel asks. A
    // type counts only on a Field's row: a signature runs over its own row.
    signal measured(real valueWidth, real typeWidth)
    function remeasure() {
        const cells = rowBody.item
        if (!tableMode || !cells)
            return
        const value = cells.valueLabel ? cells.valueLabel.implicitWidth : 0
        const type = cells.typeLabel && fieldRow ? cells.typeLabel.implicitWidth : 0
        measured(value, type)
    }
    TableView.onReused: remeasure()

    // What the box shows and whether it takes a click; the panel decides.
    property bool watchable: false
    property int watchState: Qt.Unchecked
    property bool watchEnabled: true
    property string watchName: ""
    // Said on the box while it is off.
    property string watchOffReason: ""
    // A partly ticked box: what it counts ("2 of 3 members watched"), and the
    // tooltip that says what a click does.
    property string watchTally: ""
    property string watchTallyHint: ""
    // The row's tooltip; the detail by default.
    property string hint: detail

    // The device's index of this row, whatever is current or selected. Asked
    // again whenever the row may show another node: opening or closing a
    // branch moves nodes to other rows while a row keeps its number, and a
    // reused row takes another node's roles. A tree over a filter maps it to
    // the device's model.
    readonly property var sourceIndex: {
        void treeView.rows
        void treeRow.name
        void treeRow.depth
        void treeRow.detail
        const shown = treeView.modelIndex(Qt.point(0, treeRow.row))
        const model = treeView.model
        return model && typeof model.mapToSource === "function" ? model.mapToSource(shown) : shown
    }
    // The kind the row shows: its model's, or worked out for a model without
    // one (the designer's rule for third-party trees).
    readonly property string shownKind: kind === "category" ? "group"
        : kind.length > 0 ? kind
        : !isEndpoint && !previewChild ? "group"
        : category === 2 ? "command"
        : category === 3 ? "service"
        : typeText.endsWith("]") ? "array"
        : typeText.startsWith("struct") ? "struct" : "value"
    // Selected while the tree has the keyboard, or under a host's own
    // highlight: the palette's highlight. Selected without the keyboard: the
    // quieter fill, and the row's text as at rest (family A9).
    readonly property bool selectedActive: highlighted && (treeView.activeFocus
        || !Qt.colorEqual(palette.highlight, Theme.stateSelected))
    // Palette roles, the Theme's by default: a host's palette restyles the rows.
    readonly property color textColor: selectedActive ? palette.highlightedText : palette.buttonText
    readonly property color selectedText: palette.highlightedText
    // The glyph, the type and the access marks.
    readonly property color markColor: selectedActive ? palette.highlightedText : Theme.textSecondary
    // A group: DemiBold, its count at the right.
    readonly property bool container: shownKind === "group"
    // A Field's own row (not a value inside one, not a Command or a Service):
    // the rows the Type column is measured from.
    readonly property bool fieldRow: isEndpoint && !previewChild && shownKind !== "command" && shownKind !== "service"
    // The slot the box sits in, kept on every row.
    readonly property int boxSlot: reserveWatchSpace ? Type.rowHeight : 0
    readonly property bool hasMetadata: typeText.length > 0 || accessText.length > 0
    readonly property bool narrow: contentItem.width < Type.bodyPixels * 28
    readonly property bool twoLines: !tableMode && hasMetadata && (isEndpoint || previewChild) && narrow
    readonly property bool detailsOnly: previewState === "Open Details" || previewState === "OpenDetails"
    readonly property bool limitReached: previewState === "Preview limit" || previewState === "Previewlimit"
    readonly property bool valueShown: previewsShown && (previewEligible || previewChild || detailsOnly || limitReached)
    readonly property bool foldedPreview: previewAggregate && hasChildren && !previewChild && !expanded
    readonly property bool stale: previewState === "Stale"
    readonly property bool failed: previewState === "Unavailable" || previewState === "Previewlimit" || previewState === "Preview limit"
    readonly property string metadataText: [typeText, accessText].filter(function(part) { return part.length > 0 }).join(" · ")
    readonly property string stateText: {
        if (!valueShown) return ""
        if (detailsOnly) return qsTr("Open Details")
        if (limitReached) return qsTr("Preview limit")
        switch (previewState) {
        case "Queued": return qsTr("Queued…")
        case "Reading": return qsTr("Reading…")
        // Tree previews are deliberately slow. Cache TTL is a scheduling detail,
        // not a warning or an age label on an otherwise valid cached value.
        case "Current":
        case "Stale": return ""
        case "Unavailable": return qsTr("Unavailable")
        case "Previewlimit":
        case "Preview limit": return qsTr("Preview limit")
        case "OpenDetails":
        case "Open Details": return qsTr("Open Details")
        case "Not readable": return qsTr("Not readable")
        default: return qsTr("Not read")
        }
    }
    // A folded struct shows its summary, "{Gain 1, Enabled true, …}"; open,
    // its members carry the values and its own cell is blank.
    readonly property string shownValue: !valueShown || (previewAggregate && hasChildren && expanded) ? ""
        : previewValue.length > 0 ? previewValue + (!previewAggregate && unit.length > 0 ? " " + unit : "") : "—"
    readonly property string secondaryText: [metadataText, stateText].filter(function(part) { return part.length > 0 }).join(" · ")
    readonly property string fullDescription: [hint, metadataText, shownValue, stateText, previewReason]
        .filter(function(part) { return part.length > 0 }).join("\n")
    // The name's tooltip (family A10): the path, then the row in words, an
    // enum's names, and what its value says.
    readonly property string toolTipText: [pathText.length > 0 ? pathText : name, hint, typeNames, shownValue, stateText, previewReason]
        .filter(function(part) { return part.length > 0 }).join("\n")
    readonly property color secondaryColor: highlighted ? palette.highlightedText
        : failed ? Theme.warning : Theme.textSecondary

    // The row in words, for a screen reader (family A10): "Config, Field,
    // struct with 3 members, read and write"; "measure, group, 5 Fields".
    readonly property string kindWords: shownKind === "group" ? qsTr("group")
        : shownKind === "command" ? qsTr("Command")
        : shownKind === "service" ? qsTr("Service")
        : previewChild ? (name.startsWith("[") ? qsTr("element") : qsTr("member"))
        : qsTr("Field")
    readonly property string typeWords: {
        if (typeSpoken.length > 0)
            return typeSpoken
        let found = /^struct \((\d+)\)$/.exec(typeText)
        if (found)
            return Number(found[1]) === 1 ? qsTr("struct with 1 member") : qsTr("struct with %L1 members").arg(Number(found[1]))
        found = /^enum \((\d+)\)$/.exec(typeText)
        if (found)
            return Number(found[1]) === 1 ? qsTr("enum with 1 value") : qsTr("enum with %L1 values").arg(Number(found[1]))
        found = /^(.+)\[(\d+)\]$/.exec(typeText)
        if (found)
            return qsTr("array of %L1 %2").arg(Number(found[2])).arg(found[1])
        return typeText
    }
    readonly property string pickedWords: picked ? qsTr("selected") : ""
    readonly property string accessWords: accessMark === "rw" ? qsTr("read and write")
        : accessMark === "r-" ? qsTr("read only")
        : accessMark === "-w" ? qsTr("write only")
        : accessMark === "--" ? qsTr("no access") : ""

    // The box's reason while it is off: the row's own first, then polling.
    readonly property string boxReason: watchRefusal.length > 0 ? watchRefusal : watchOffReason
    // What the box says to a keyboard or screen reader user, who never hovers it.
    readonly property string boxWords: !watchable ? ""
        : !watchBox.enabled ? boxReason
        : watchTally.length > 0 ? watchTally
        : watchState === Qt.Checked ? qsTr("watched") : qsTr("not watched")
    signal watchToggled()
    // A right click on the row: the panel opens its menu there.
    signal contextMenuRequested(real x, real y)

    background: Rectangle {
        // The selection quieter while the tree has no keyboard, unless a host
        // chose its own highlight; hover the Theme's hover fill.
        color: treeRow.selectedActive ? treeRow.palette.highlight
             : treeRow.highlighted ? Theme.stateSelectedInactive
             : treeRow.hovered ? Theme.stateHover
             : treeRow.tableMode && treeRow.row % 2 === 1 ? treeRow.palette.alternateBase : "transparent"
        // Indent guides (as Details'): one 1 px line under each ancestor's
        // chevron; the current branch's brighter, the one thing the cursor
        // moves. One step for the whole tree, so a branch's guide runs straight.
        Repeater {
            model: treeRow.tableMode ? treeRow.depth : 0
            Rectangle {
                required property int index
                objectName: "indentGuide"
                x: Math.round(treeRow.leftMargin + index * treeRow.indentation + Type.indent / 2)
                width: 1
                height: treeRow.height
                readonly property bool active: index === treeRow.guideLevel
                    && treeRow.row >= treeRow.guideFrom && treeRow.row <= treeRow.guideTo
                color: active ? Theme.outline : Theme.outlineVariant
                Accessible.ignored: true
            }
        }
    }
    implicitWidth: treeView.width
    implicitHeight: twoLines ? Math.max(Type.rowHeight, nameMetrics.height + metadataMetrics.height + Type.tight * 2) : Type.rowHeight
    rightPadding: tableMode ? 0 : Type.padding
    // No gap after the chevron: the box slot follows it at once, as the
    // designer's anatomy has it (6 | depth × 16 | 16 | box 22 | glyph 16 | 4 |
    // name), the panel's padding the 6 on either side, as Device files' rows.
    spacing: tableMode ? 0 : 4
    leftMargin: 0
    // One step for every row of the tree (the designer's review, 14.1): the
    // panel chooses it per descriptor and width, never per row, so a child
    // never indents less than its parent and opening moves nothing sideways.
    indentation: tableMode ? indentStep : Type.indent
    indicator: TreeChevron { row: treeRow }
    text: name
    Accessible.role: Accessible.TreeItem
    Accessible.name: tableMode ? [name, kindWords, container ? countFull : typeWords, accessWords, pickedWords]
        .filter(function(part) { return part.length > 0 }).join(", ") : name
    Accessible.selected: picked
    Accessible.description: [boxWords, fullDescription].filter(function(part) { return part.length > 0 }).join("\n")
    Accessible.expandable: hasChildren
    Accessible.expanded: expanded
    FontMetrics { id: nameMetrics; font: treeRow.font }
    FontMetrics { id: metadataMetrics; font.family: treeRow.font.family; font.pointSize: Type.caption }

    contentItem: Item {
        implicitHeight: rowBody.item ? rowBody.item.implicitHeight : Type.rowHeight
        RowCheckBox {
            id: watchBox
            objectName: "endpointWatchCheck"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            visible: treeRow.watchable && (!treeRow.tableMode || treeRow.leftPadding + treeRow.boxSlot < treeRow.nameColumnWidth)
            enabled: treeRow.watchEnabled && treeRow.watchRefusal.length === 0
            tristate: true
            checkState: treeRow.watchState
            // The provider decides; the box shows what it decided.
            nextCheckState: function() { return treeRow.watchState }
            onClicked: {
                treeRow.watchToggled()
                checkState = Qt.binding(function() { return treeRow.watchState })
            }
            Accessible.name: treeRow.watchName
            Accessible.description: watchBox.enabled ? treeRow.watchTally : treeRow.boxReason
            edgeColor: treeRow.textColor
            indicator.objectName: "endpointWatchBox"
            HoverHandler { id: boxHover }
            PlainToolTip {
                objectName: "endpointWatchReason"
                visible: boxHover.hovered && (watchBox.enabled ? treeRow.watchTallyHint.length > 0
                                                               : treeRow.boxReason.length > 0)
                text: watchBox.enabled ? treeRow.watchTallyHint : treeRow.boxReason
                y: treeRow.height + 2
                delay: 700
            }
        }
        Loader {
            id: rowBody
            anchors.fill: parent
            sourceComponent: treeRow.tableMode ? tableContent : legacyContent
        }
        Component {
            id: tableContent
            Item {
                id: cells
                implicitHeight: Type.rowHeight
                readonly property Item valueLabel: tableValue
                readonly property Item typeLabel: tableType
                // In the content item's coordinates: its right edge, then each
                // column's left edge, right to left.
                readonly property real rightEdge: treeRow.width - treeRow.leftPadding - treeRow.rightClearance
                readonly property real accessX: rightEdge - treeRow.accessColumnWidth
                readonly property real typeX: accessX - (treeRow.accessColumnWidth > 0 ? treeRow.columnGap : 0)
                                              - treeRow.typeColumnWidth
                readonly property real valueX: typeX - (treeRow.typeColumnWidth > 0 ? treeRow.columnGap : 0)
                                               - treeRow.valueColumnWidth
                // The glyph after the box slot, the name after the glyph.
                readonly property real nameX: treeRow.boxSlot + Type.iconSize + Type.spacing
                // A value this row shows: the type never crosses it.
                readonly property bool valueTaken: treeRow.valueColumnWidth > 0 && treeRow.valueShown && !treeRow.container
                // The type runs on over a blank access cell (design D2), and
                // moves its start left only as far as its own name allows:
                // first the name down to its floor, only then the type elides.
                readonly property real typeRight: treeRow.accessColumnWidth > 0 && treeRow.accessMark !== ""
                    ? typeX + treeRow.typeColumnWidth : rightEdge
                readonly property real leftLimit: valueTaken ? valueX + treeRow.valueColumnWidth + treeRow.columnGap
                    : nameX + Math.min(tableName.implicitWidth, Type.nameFloor) + treeRow.columnGap
                readonly property real typeLeft: Math.max(leftLimit, Math.min(typeX, typeRight - tableType.implicitWidth))
                readonly property real nameRight: treeRow.container
                    ? rightEdge - (tableCount.visible ? tableCount.implicitWidth + treeRow.columnGap : 0)
                    : (valueTaken ? valueX : typeLeft) - treeRow.columnGap
                // A group's count gives way before its name drops below its
                // floor: whole parts from the right, "· …" marking the cut;
                // the first part always stays.
                readonly property real countRoom: rightEdge - nameX - Math.min(tableName.implicitWidth, Type.nameFloor)
                                                  - treeRow.columnGap
                readonly property string countShown: {
                    const parts = treeRow.countParts || []
                    if (parts.length === 0)
                        return ""
                    const sep = " · "
                    for (let n = parts.length; n >= 1; --n) {
                        const text = parts.slice(0, n).join(sep) + (n < parts.length ? sep + "…" : "")
                        if (countMetrics.advanceWidth(text) <= countRoom)
                            return text
                    }
                    return parts[0] + (parts.length > 1 ? sep + "…" : "")
                }
                FontMetrics { id: countMetrics; font.pointSize: Type.caption }
                // A find's run, marked in the name or in the Type cell:
                // info, underlined; the rest escaped, so a name never reads
                // as markup.
                function escaped(text) {
                    return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                }
                function marked(text, where) {
                    const run = treeRow.matchRun
                    if (!treeRow.matched || run.in !== where || run.start < 0 || run.start + run.length > text.length)
                        return ""
                    const tint = treeRow.selectedActive ? treeRow.palette.highlightedText : Theme.info
                    return escaped(text.substring(0, run.start)) + "<u><font color=\"" + tint + "\">"
                        + escaped(text.substring(run.start, run.start + run.length)) + "</font></u>"
                        + escaped(text.substring(run.start + run.length))
                }
                KindIcon {
                    id: kindIcon
                    x: treeRow.boxSlot
                    anchors.verticalCenter: parent.verticalCenter
                    kind: treeRow.shownKind
                    color: treeRow.markColor
                }
                Label {
                    id: tableName
                    objectName: "endpointRowName"
                    x: cells.nameX
                    width: Math.max(0, cells.nameRight - x)
                    anchors.verticalCenter: parent.verticalCenter
                    // The matched run marked while a find is on.
                    readonly property string marked: cells.marked(treeRow.name, "name")
                    textFormat: marked.length > 0 ? Text.StyledText : Text.PlainText
                    elide: Text.ElideMiddle
                    text: marked.length > 0 ? marked : treeRow.name
                    color: treeRow.textColor
                    font.weight: treeRow.container ? Type.headingWeight : Type.bodyWeight
                    HoverHandler { id: tableNameHover }
                    PlainToolTip {
                        objectName: "endpointNameToolTip"
                        visible: tableNameHover.hovered
                        text: tableNameHover.hovered ? treeRow.toolTipText : ""
                        delay: 700
                    }
                }
                // A group's count in words at the right edge, where the
                // access marks end: "7 Fields · 1 Command"; the whole of it,
                // with the groups under it, on hover.
                Label {
                    id: tableCount
                    objectName: "endpointRowCount"
                    visible: treeRow.container && cells.countShown.length > 0
                    anchors.right: parent.left
                    anchors.rightMargin: -cells.rightEdge
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    font.pointSize: Type.caption
                    text: cells.countShown
                    color: treeRow.selectedActive ? treeRow.selectedText
                        : treeRow.highlighted ? Theme.textSecondary : Theme.textMuted
                    Accessible.name: treeRow.countFull
                    HoverHandler { id: tableCountHover }
                    PlainToolTip {
                        objectName: "endpointRowCountToolTip"
                        visible: tableCountHover.hovered && treeRow.countFull.length > 0
                        text: treeRow.countFull
                        delay: 700
                    }
                }
                Label {
                    id: tableValue
                    objectName: "endpointRowValue"
                    visible: treeRow.valueColumnWidth > 0 && !treeRow.container
                    x: cells.valueX
                    width: treeRow.valueColumnWidth
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    // Numbers and bools stand right; a struct's summary reads left.
                    horizontalAlignment: treeRow.previewAggregate && !treeRow.previewChild ? Text.AlignLeft : Text.AlignRight
                    font.family: Type.fixedFamily
                    font.italic: treeRow.stale || treeRow.failed
                    text: treeRow.shownValue
                    color: treeRow.selectedActive ? treeRow.selectedText
                        : treeRow.stale || treeRow.failed || treeRow.previewValue.length === 0 ? Theme.textMuted
                        : treeRow.textColor
                    onImplicitWidthChanged: treeRow.remeasure()
                    HoverHandler { id: tableValueHover }
                    Accessible.description: treeRow.fullDescription
                    PlainToolTip { visible: tableValueHover.hovered; text: treeRow.fullDescription; delay: 700 }
                }
                // Left at the column's x in the fixed face; never hidden: a
                // long one moves left into its own row's room, then elides.
                // A named type in two tones, its scope muted; "…::" while
                // the column gives its scopes way (14.2).
                TypeLabel {
                    id: tableType
                    objectName: "endpointRowType"
                    visible: treeRow.typeColumnWidth > 0 && !treeRow.container
                    x: cells.typeLeft
                    width: Math.max(0, cells.typeRight - cells.typeLeft)
                    anchors.verticalCenter: parent.verticalCenter
                    typeText: treeRow.typeText
                    shortScopes: treeRow.typeScopesCut
                    // A row found by its named type marks the run here, and
                    // shows its scope.
                    run: treeRow.matched && treeRow.matchRun.in === "type" ? treeRow.matchRun : ({})
                    nameColor: treeRow.markColor
                    scopeColor: treeRow.selectedActive ? treeRow.palette.highlightedText
                        : treeRow.highlighted ? Theme.textSecondary : Theme.textMuted
                    runColor: treeRow.selectedActive ? treeRow.palette.highlightedText : Theme.info
                    Component.onCompleted: treeRow.remeasure()
                    onImplicitWidthChanged: treeRow.remeasure()
                    HoverHandler { id: tableTypeHover }
                    // The full name whenever something was cut.
                    PlainToolTip {
                        objectName: "endpointRowTypeToolTip"
                        visible: tableTypeHover.hovered && (tableType.truncated || tableType.cutShown)
                        text: treeRow.typeText
                        delay: 700
                    }
                }
                // The Field's access, letter by letter, a dash for what it lacks.
                Item {
                    id: tableAccess
                    objectName: "endpointRowAccess"
                    visible: treeRow.accessColumnWidth > 0 && !treeRow.container
                    x: cells.accessX
                    width: treeRow.accessColumnWidth
                    height: parent.height
                    readonly property string text: treeRow.accessMark.length > 0 ? treeRow.accessMark : treeRow.accessText
                    Accessible.role: Accessible.StaticText
                    Accessible.name: treeRow.accessText
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: treeRow.accessMark.length > 0
                        Repeater {
                            model: 2
                            Label {
                                required property int index
                                text: treeRow.accessMark.charAt(index)
                                font.family: Type.fixedFamily
                                font.pointSize: Type.caption
                                color: treeRow.selectedActive ? treeRow.selectedText
                                    : treeRow.highlighted ? Theme.textSecondary
                                    : text === "-" ? Theme.textDisabled : Theme.textSecondary
                                Accessible.ignored: true
                            }
                        }
                    }
                    HoverHandler { id: tableAccessHover }
                    PlainToolTip {
                        visible: tableAccessHover.hovered && treeRow.accessMark.length > 0
                        text: treeRow.accessMark === "rw" ? qsTr("read and write") : treeRow.accessMark === "r-" ? qsTr("read only")
                            : treeRow.accessMark === "-w" ? qsTr("write only") : qsTr("no access")
                        delay: 700
                    }
                }
            }
        }
        Component {
            id: legacyContent
            ColumnLayout {
            id: rowContent
            anchors.left: parent.left
            anchors.leftMargin: treeRow.boxSlot + Type.tight
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Type.tight
            RowLayout {
                Layout.fillWidth: true
                spacing: Type.spacing
                Label {
                    id: nameLabel
                    objectName: "endpointRowName"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    text: treeRow.name
                    color: treeRow.textColor
                }
                Label {
                    objectName: "endpointRowValue"
                    visible: treeRow.shownValue.length > 0
                    Layout.maximumWidth: Math.max(0, treeRow.contentItem.width * (treeRow.twoLines ? 0.53 : 0.34))
                    Layout.minimumWidth: 0
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    font.family: Type.fixedFamily
                    text: treeRow.shownValue
                    color: treeRow.highlighted ? treeRow.palette.highlightedText
                         : treeRow.failed ? Theme.textMuted : treeRow.textColor
                }
                Label {
                    id: inlineMetadata
                    objectName: "endpointRowInlineMetadata"
                    visible: !treeRow.twoLines && treeRow.secondaryText.length > 0
                    Layout.maximumWidth: Math.max(0, treeRow.contentItem.width * 0.38)
                    Layout.minimumWidth: 0
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.pointSize: Type.caption
                    text: treeRow.secondaryText
                    color: treeRow.secondaryColor
                }
            }
            Label {
                id: stackedMetadata
                objectName: "endpointRowMetadata"
                visible: treeRow.twoLines
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pointSize: Type.caption
                text: treeRow.secondaryText
                color: treeRow.secondaryColor
            }
            }
        }
    }

    Rectangle {
        objectName: "endpointFocusIndicator"
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.focusRing
        border.width: 2
        radius: 3
        visible: treeRow.treeView.activeFocus && treeRow.current
    }
    // Under the row it describes, not over it.
    PlainToolTip {
        objectName: "endpointToolTip"
        visible: !treeRow.tableMode && treeRow.hovered && treeRow.fullDescription.length > 0
        text: treeRow.fullDescription
        y: treeRow.height + 2
        delay: 700
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(point) { treeRow.contextMenuRequested(point.position.x, point.position.y) }
    }
}
