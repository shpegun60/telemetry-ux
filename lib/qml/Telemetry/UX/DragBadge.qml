// What rides on the cursor while rows of the Device tree are dragged (the
// designer's section 16.3): the first row's glyph and path label, at most
// 240 px, then its count words, a Field's type, or "and 3 more". A platform
// drag cannot redraw it, so it is made once, at the press, as an image
// (Drag.imageSource); the target says the outcome in its own unit. The
// surface, a 1 px outline and the caption type. MIT.
import QtQuick
import QtQuick.Controls

Rectangle {
    id: badge
    objectName: "dragBadge"
    // The first picked row: its kind (KindIcon's) and path.
    property string kind: ""
    property string path: ""
    // Under the path: "23 Fields · 3 Commands", "adc::Config", "and 3 more".
    property string detail: ""
    // The image grabbed last, for Drag.imageSource; empty until made.
    property url image: ""
    readonly property real most: 240
    signal made(url image)

    // Grabs what it shows now; made() carries the image's url.
    function make(kind, path, detail) {
        badge.kind = kind
        badge.path = path
        badge.detail = detail
        badge.grabToImage(function(result) {
            badge.image = result.url
            badge.made(result.url)
        })
    }

    width: Math.min(most, Math.ceil(Math.max(Type.iconSize + Type.spacing + pathLabel.implicitWidth,
                                               detailText.implicitWidth)) + Type.padding * 2)
    height: column.implicitHeight + Type.tight * 2
    color: Theme.surface
    border.width: 1
    border.color: Theme.outline
    radius: Type.radius
    Accessible.ignored: true
    Column {
        id: column
        x: Type.padding
        y: Type.tight
        width: badge.width - Type.padding * 2
        spacing: 0
        Row {
            spacing: Type.spacing
            width: parent.width
            KindIcon {
                anchors.verticalCenter: parent.verticalCenter
                kind: badge.kind
                color: Theme.textSecondary
            }
            PathLabel {
                id: pathLabel
                objectName: "dragBadgePath"
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - Type.iconSize - Type.spacing
                path: badge.path
                toolTipEnabled: false
                font.pointSize: Type.caption
            }
        }
        Label {
            id: detailText
            objectName: "dragBadgeDetail"
            width: parent.width
            visible: text.length > 0
            text: badge.detail
            textFormat: Text.PlainText
            elide: Text.ElideRight
            font.pointSize: Type.caption
            color: Theme.textMuted
        }
    }
}
