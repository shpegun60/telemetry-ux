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
    readonly property color textColor: highlighted ? palette.highlightedText : palette.buttonText
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
        if (foldedPreview) return qsTr("Expand to preview")
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
    readonly property string shownValue: !valueShown || foldedPreview ? ""
        : previewValue.length > 0 ? previewValue + (!previewAggregate && unit.length > 0 ? " " + unit : "") : "—"
    readonly property string secondaryText: [metadataText, stateText].filter(function(part) { return part.length > 0 }).join(" · ")
    readonly property string fullDescription: [hint, metadataText, shownValue, stateText, previewReason]
        .filter(function(part) { return part.length > 0 }).join("\n")
    readonly property color secondaryColor: highlighted ? palette.highlightedText
        : failed ? Theme.warning : Theme.textSecondary

    signal watchToggled()
    // A right click on the row: the panel opens its menu there.
    signal contextMenuRequested(real x, real y)

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
            enabled: treeRow.watchEnabled
            tristate: true
            checkState: treeRow.watchState
            // The provider decides; the box shows what it decided.
            nextCheckState: function() { return treeRow.watchState }
            onClicked: {
                treeRow.watchToggled()
                checkState = Qt.binding(function() { return treeRow.watchState })
            }
            Accessible.name: treeRow.watchName
            Accessible.description: watchBox.enabled ? "" : treeRow.watchOffReason
            edgeColor: treeRow.textColor
            indicator.objectName: "endpointWatchBox"
            HoverHandler { id: boxHover }
            PlainToolTip {
                visible: boxHover.hovered && !watchBox.enabled && treeRow.watchOffReason.length > 0
                text: treeRow.watchOffReason
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
                implicitHeight: Type.rowHeight
                Item {
                    x: -treeRow.leftPadding
                    width: treeRow.nameColumnWidth
                    height: parent.height
                    clip: true
                    Label {
                        id: tableName
                        objectName: "endpointRowName"
                        x: treeRow.leftPadding + treeRow.boxSlot + Type.tight
                        width: Math.max(0, parent.width - x - Type.padding)
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        text: treeRow.name
                        color: treeRow.textColor
                        HoverHandler { id: tableNameHover }
                        PlainToolTip { visible: tableNameHover.hovered; text: treeRow.fullDescription; delay: 700 }
                    }
                }
                Label {
                    id: tableValue
                    objectName: "endpointRowValue"
                    x: treeRow.nameColumnWidth - treeRow.leftPadding + Type.padding
                    width: Math.max(0, treeRow.valueColumnWidth - 2 * Type.padding)
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                    font.family: Type.fixedFamily
                    text: treeRow.shownValue
                    color: treeRow.highlighted ? treeRow.palette.highlightedText
                        : treeRow.failed ? Theme.textMuted : treeRow.textColor
                    HoverHandler { id: tableValueHover }
                    Accessible.description: treeRow.fullDescription
                    PlainToolTip { visible: tableValueHover.hovered; text: treeRow.fullDescription; delay: 700 }
                }
                Label {
                    id: tableType
                    objectName: "endpointRowType"
                    x: treeRow.nameColumnWidth + treeRow.valueColumnWidth - treeRow.leftPadding + Type.padding
                    width: Math.max(0, treeRow.typeColumnWidth - 2 * Type.padding)
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.family: Type.fixedFamily
                    font.pointSize: Type.caption
                    text: treeRow.typeText
                    color: treeRow.highlighted ? treeRow.palette.highlightedText : Theme.textSecondary
                    HoverHandler { id: tableTypeHover }
                    PlainToolTip { visible: tableTypeHover.hovered && tableType.truncated; text: treeRow.typeText; delay: 700 }
                }
                Item {
                    id: tableAccess
                    objectName: "endpointRowAccess"
                    x: treeRow.nameColumnWidth + treeRow.valueColumnWidth + treeRow.typeColumnWidth - treeRow.leftPadding + Type.padding
                    width: Math.max(0, treeRow.accessColumnWidth - 2 * Type.padding)
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
                                color: treeRow.highlighted ? treeRow.palette.highlightedText
                                    : text === "-" ? Theme.textDisabled : Theme.textSecondary
                                Accessible.ignored: true
                            }
                        }
                    }
                    Label {
                        anchors.fill: parent
                        visible: treeRow.accessMark.length === 0
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.pointSize: Type.caption
                        text: treeRow.accessText
                        color: treeRow.highlighted ? treeRow.palette.highlightedText : Theme.textSecondary
                        Accessible.ignored: true
                    }
                    HoverHandler { id: tableAccessHover }
                    PlainToolTip { visible: tableAccessHover.hovered; text: treeRow.accessText; delay: 700 }
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
        border.color: treeRow.highlighted ? treeRow.palette.highlightedText : treeRow.palette.text
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
