// One row of the Device tree: a watch box where the Watchlist can hold what
// the row stands for, and the endpoint's or group's name. The row is the
// type scale's row height; the box is 16 px inside a 24 px slot that every
// row keeps, so names line up whether a row has a box or not. The current row
// is framed while the tree has the keyboard. MIT.
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
    // A category's or group's count at the row's right edge ("70", "4 Fields").
    property string countText: ""
    // Why the box is off when the Watchlist cannot hold this row (yet).
    property string watchRefusal: ""
    // Measured texts, so the panel can size its columns to them: each label's
    // own width, reported again when the row is reused or the panel asks.
    signal measured(real valueWidth, real typeWidth)
    function remeasure() {
        const cells = rowBody.item
        if (!tableMode || !cells)
            return
        const value = cells.valueLabel ? cells.valueLabel.implicitWidth : 0
        const type = cells.typeLabel ? cells.typeLabel.implicitWidth : 0
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
    // The row's tooltip; the detail by default.
    property string hint: detail

    // The device's index of this row, whatever is current or selected. Asked
    // again whenever the row may show another node: opening or closing a
    // branch moves nodes to other rows while a row keeps its number, and a
    // reused row takes another node's roles.
    readonly property var sourceIndex: {
        void treeView.rows
        void treeRow.name
        void treeRow.depth
        void treeRow.detail
        return treeView.modelIndex(Qt.point(0, treeRow.row))
    }
    // Palette roles, the Theme's by default: a host's palette restyles the rows.
    readonly property color textColor: highlighted ? palette.highlightedText : palette.buttonText
    readonly property color selectedText: palette.highlightedText
    // A category or a group: DemiBold, its count at the right.
    readonly property bool container: !isEndpoint && !previewChild && hasChildren
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
    readonly property color secondaryColor: highlighted ? palette.highlightedText
        : failed ? Theme.warning : Theme.textSecondary

    // The box's reason while it is off: the row's own first, then polling.
    readonly property string boxReason: watchRefusal.length > 0 ? watchRefusal : watchOffReason
    signal watchToggled()
    // A right click on the row: the panel opens its menu there.
    signal contextMenuRequested(real x, real y)

    background: Rectangle {
        // The selection quieter while the tree has no keyboard, unless a host
        // chose its own highlight; hover a tint of the text over any base.
        color: treeRow.highlighted
               ? (treeRow.treeView.activeFocus || !Qt.colorEqual(treeRow.palette.highlight, Theme.stateSelected)
                  ? treeRow.palette.highlight : Theme.stateSelectedInactive)
             : treeRow.hovered ? Qt.rgba(treeRow.palette.text.r, treeRow.palette.text.g, treeRow.palette.text.b, 0.07)
             : treeRow.tableMode && treeRow.row % 2 === 1 ? treeRow.palette.alternateBase : "transparent"
    }
    implicitWidth: treeView.width
    implicitHeight: twoLines ? Math.max(Type.rowHeight, nameMetrics.height + metadataMetrics.height + Type.tight * 2) : Type.rowHeight
    rightPadding: tableMode ? 0 : Type.padding
    readonly property real indentRoom: Math.max(0, nameColumnWidth - boxSlot - Type.tight - Type.padding
        - Type.bodyPixels * 4 - Type.indent - spacing)
    indentation: tableMode && depth > 0 ? Math.min(Type.indent, indentRoom / depth) : Type.indent
    // The panel's padding is the margin; Device files' rows line up with these.
    leftMargin: 0
    indicator: TreeChevron { row: treeRow }
    text: name
    Accessible.role: Accessible.TreeItem
    Accessible.name: name
    Accessible.description: fullDescription
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
            Accessible.description: watchBox.enabled ? "" : treeRow.boxReason
            edgeColor: treeRow.textColor
            indicator.objectName: "endpointWatchBox"
            HoverHandler { id: boxHover }
            PlainToolTip {
                objectName: "endpointWatchReason"
                visible: boxHover.hovered && !watchBox.enabled && treeRow.boxReason.length > 0
                text: treeRow.boxReason
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
                readonly property real rightEdge: treeRow.width - treeRow.leftPadding - Type.padding - treeRow.rightClearance
                readonly property real accessX: rightEdge - treeRow.accessColumnWidth
                readonly property real typeX: accessX - (treeRow.accessColumnWidth > 0 ? treeRow.columnGap : 0)
                                              - treeRow.typeColumnWidth
                readonly property real valueX: typeX - (treeRow.typeColumnWidth > 0 ? treeRow.columnGap : 0)
                                               - treeRow.valueColumnWidth
                readonly property real nameRight: treeRow.container
                    ? rightEdge - (tableCount.visible ? tableCount.implicitWidth + treeRow.columnGap : 0)
                    : valueX - (treeRow.valueColumnWidth > 0 ? treeRow.columnGap : 0)
                Label {
                    id: tableName
                    objectName: "endpointRowName"
                    x: treeRow.boxSlot + Type.tight
                    width: Math.max(0, cells.nameRight - x)
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    text: treeRow.name
                    color: treeRow.textColor
                    font.weight: treeRow.container ? Type.headingWeight : Font.Normal
                    HoverHandler { id: tableNameHover }
                    PlainToolTip { visible: tableNameHover.hovered; text: treeRow.fullDescription; delay: 700 }
                }
                // A category's bare number, a group's Fields: at the right edge.
                Label {
                    id: tableCount
                    objectName: "endpointRowCount"
                    visible: treeRow.container && treeRow.countText.length > 0
                    anchors.right: parent.left
                    anchors.rightMargin: -cells.rightEdge
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    font.pointSize: Type.caption
                    text: treeRow.countText
                    color: treeRow.highlighted ? treeRow.selectedText : Theme.textMuted
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
                    color: treeRow.highlighted ? treeRow.selectedText
                        : treeRow.stale || treeRow.failed || treeRow.previewValue.length === 0 ? Theme.textMuted
                        : treeRow.textColor
                    onImplicitWidthChanged: treeRow.remeasure()
                    HoverHandler { id: tableValueHover }
                    Accessible.description: treeRow.fullDescription
                    PlainToolTip { visible: tableValueHover.hovered; text: treeRow.fullDescription; delay: 700 }
                }
                Label {
                    id: tableType
                    objectName: "endpointRowType"
                    visible: treeRow.typeColumnWidth > 0 && !treeRow.container
                    x: cells.typeX
                    width: treeRow.typeColumnWidth
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    text: treeRow.typeText
                    color: treeRow.highlighted ? treeRow.selectedText : Theme.textSecondary
                    Component.onCompleted: treeRow.remeasure()
                    onImplicitWidthChanged: treeRow.remeasure()
                    HoverHandler { id: tableTypeHover }
                    PlainToolTip { visible: tableTypeHover.hovered && tableType.truncated; text: treeRow.typeText; delay: 700 }
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
                                color: treeRow.highlighted ? treeRow.selectedText
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
