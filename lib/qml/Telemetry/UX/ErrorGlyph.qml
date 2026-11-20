// A failure's 12 px glyph: a ring with "!" in the error colour. Details, the
// Batch and the Watchlist mark a failure with it, always beside its words, so
// colour never carries the failure alone. Pure QML, so a host needs none of
// the module's C++ types for it; a mark, never a button. MIT.
import QtQuick
import QtQuick.Shapes

Shape {
    id: glyph
    objectName: "errorGlyph"
    property color color: Theme.error
    readonly property real s: width / 12
    implicitWidth: Math.round(Type.bodyPixels * 0.9)
    implicitHeight: implicitWidth
    preferredRendererType: Shape.CurveRenderer
    Accessible.ignored: true
    ShapePath {
        strokeColor: glyph.color
        strokeWidth: Math.max(1, glyph.s * 1.2)
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        startX: glyph.s * 6
        startY: glyph.s * 0.9
        PathArc { x: glyph.s * 6; y: glyph.s * 11.1; radiusX: glyph.s * 5.1; radiusY: glyph.s * 5.1 }
        PathArc { x: glyph.s * 6; y: glyph.s * 0.9; radiusX: glyph.s * 5.1; radiusY: glyph.s * 5.1 }
        PathMove { x: glyph.s * 6; y: glyph.s * 3.4 }
        PathLine { x: glyph.s * 6; y: glyph.s * 6.9 }
        PathMove { x: glyph.s * 6; y: glyph.s * 8.9 }
        PathLine { x: glyph.s * 6; y: glyph.s * 9.0 }
    }
}
