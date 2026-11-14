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
    readonly property bool twoLines: hasMetadata && (isEndpoint || previewChild) && narrow
    readonly property bool detailsOnly: previewState === "Open Details" || previewState === "OpenDetails"
    readonly property bool limitReached: previewState === "Preview limit" || previewState === "Previewlimit"
    readonly property bool valueShown: previewsShown && (previewEligible || previewChild || detailsOnly || limitReached)
    readonly property bool foldedPreview: previewAggregate && hasChildren && !previewChild && !expanded
    readonly property bool stale: previewState === "Stale"
    readonly property bool failed: previewState === "Unavailable" || previewState === "Previewlimit" || previewState === "Preview limit"
    readonly property string metadataText: [typeText, accessText].filter(function(part) { return part.length > 0 }).join(" · ")
    readonly property string ageText: {
        if (previewAgeMs < 0 || !isFinite(previewAgeMs)) return ""
        if (previewAgeMs < 1000) return qsTr("<1 s")
        if (previewAgeMs < 60000) return qsTr("%1 s").arg(Math.floor(previewAgeMs / 1000))
        if (previewAgeMs < 3600000) return qsTr("%1 min").arg(Math.floor(previewAgeMs / 60000))
        return qsTr("%1 h").arg(Math.floor(previewAgeMs / 3600000))
    }
    readonly property string stateText: {
        if (!valueShown) return ""
        if (detailsOnly) return qsTr("Open Details")
        if (limitReached) return qsTr("Preview limit")
        if (foldedPreview) return qsTr("Expand to preview")
        switch (previewState) {
        case "Queued": return qsTr("Queued…")
        case "Reading": return qsTr("Reading…")
        case "Current": return ageText
        case "Stale": return ageText.length ? qsTr("Stale · %1").arg(ageText) : qsTr("Stale")
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
    indentation: Type.indent
    // The panel's padding is the margin; Device files' rows line up with these.
    leftMargin: 0
    indicator: TreeChevron { row: treeRow }
    text: name
    Accessible.role: Accessible.TreeItem
    Accessible.name: name
    Accessible.description: fullDescription

    contentItem: Item {
        implicitHeight: rowContent.implicitHeight
        RowCheckBox {
            id: watchBox
            objectName: "endpointWatchCheck"
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            visible: treeRow.watchable
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
                         : treeRow.stale || treeRow.failed ? Theme.textMuted : treeRow.textColor
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
        FontMetrics { id: nameMetrics; font: nameLabel.font }
        FontMetrics { id: metadataMetrics; font: stackedMetadata.font }
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
        visible: treeRow.hovered && treeRow.fullDescription.length > 0
        text: treeRow.fullDescription
        y: treeRow.height + 2
        delay: 700
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(point) { treeRow.contextMenuRequested(point.position.x, point.position.y) }
    }
}
