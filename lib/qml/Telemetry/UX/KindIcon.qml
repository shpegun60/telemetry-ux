// What a tree row is, as one monochrome glyph in its own slot (the
// designer's, 18 November 2026): a folder for a category or a group, braces
// for a struct, brackets for an array, a ring for a scalar, one arrow for a
// Command, two for a Service. The trees share it, so a struct looks alike in
// each. A mark, never a button: no hover, no click, and a screen reader hears
// the kind in the row's own words instead. MIT.
import QtQuick

ToolIcon {
    id: glyph
    objectName: "kindIcon"
    // The row's kind: "category", "group", "struct", "array", "value",
    // "command" or "service"; the Watchlist's "element" reads as a struct.
    property string kind: ""
    name: kind === "category" || kind === "group" ? "group"
        : kind === "struct" || kind === "element" ? "struct"
        : kind === "array" || kind === "value" || kind === "command" || kind === "service" ? kind
        : ""
    width: Type.iconSize
    height: Type.iconSize
    color: Theme.textSecondary
    Accessible.ignored: true
    // Right to left only the Command's arrow turns: the others have no direction.
    transform: Scale {
        origin.x: glyph.width / 2
        xScale: glyph.name === "command" && glyph.LayoutMirroring.enabled ? -1 : 1
    }
}
