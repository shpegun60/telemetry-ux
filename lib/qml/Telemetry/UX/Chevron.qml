// A chevron drawn as one stroke, sized from the type scale: pointing right,
// turned down while `open`. TreeChevron places it in a tree row; anything that
// folds (the Watchlist's title) uses it alone. Pure QML, so a host needs none
// of the module's C++ types for it. MIT.
import QtQuick
import QtQuick.Shapes

Shape {
    id: glyph
    property bool open: false
    // Right to left it points left while closed.
    property bool mirrored: false
    property color color: Theme.textSecondary
    implicitWidth: Type.iconSize
    implicitHeight: Type.iconSize
    rotation: open ? 90 : (mirrored ? 180 : 0)
    preferredRendererType: Shape.CurveRenderer
    Accessible.ignored: true
    ShapePath {
        strokeColor: glyph.color
        strokeWidth: Math.max(1.5, glyph.width / 12)
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        startX: glyph.width * 0.4
        startY: glyph.height * 0.25
        PathLine { x: glyph.width * 0.65; y: glyph.height * 0.5 }
        PathLine { x: glyph.width * 0.4; y: glyph.height * 0.75 }
    }
}
