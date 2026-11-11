// Colour roles for the Telemetry panels and the programs built on them. MIT.
//
// A role names what a colour does, not how it looks, so a host can restyle every
// panel by assigning roles at startup (before its window loads). The defaults are
// a dark set whose text and outline pairs meet WCAG 2.2 contrast on every surface
// they are used on; tests/theme_contrast checks each pair. Names avoid the `on…`
// prefix QML would read as a signal handler: text on a role is `…Foreground`.
pragma Singleton
import QtQuick

QtObject {
    // Surfaces, darkest first.
    property color surfaceWindow: "#11151c"   // window, docks, cards, field fill
    property color surface: "#17202b"         // popovers, panes
    property color surfaceBar: "#1a2533"      // header and status bar
    property color surfaceTitleBar: "#263b4c" // dock title bars
    property color surfaceDisabled: "#1b2533" // disabled controls
    property color surfaceAlternate: "#151d27" // every other row of a long list
    property color shadow: "#0b0f14"           // shadows and the deepest edge of controls

    // Interaction states laid over a surface.
    property color stateHover: "#1f2b3a"
    property color statePressed: "#233349"
    property color stateSelected: "#215a68"
    property color selectedForeground: "#ffffff"
    // A selection whose fill is its only cue (the hex view): 3:1 on its own,
    // where stateSelected is a row's quiet state.
    property color selectionFill: "#2f7d91"
    // A codec's region in the hex view: decorative, its name says what it is.
    property color regionTint: "#1c2a3a"

    // Text.
    property color textPrimary: "#e8edf5"
    property color textSecondary: "#9fb0c3"
    property color textMuted: "#8c9fb3"       // section labels, times, descriptions, placeholders
    property color textDisabled: "#6b7a8c"

    // Lines.
    property color outline: "#647f9e"         // field and selector borders: 3:1 or more
    property color outlineVariant: "#2a3646"  // separators, disabled borders
    property color focusRing: "#6fd4bf"       // 2 px focus frames, drawn 3 px outside a
                                              // control so they contrast with what surrounds it

    // The primary action, and secondary buttons.
    property color accent: "#6fd4bf"
    property color accentPressed: "#4fb8a3"
    property color accentForeground: "#06231d"
    property color buttonFill: "#2a3c52"
    property color buttonFillPressed: "#33475f"

    // States, each a foreground and its container. Colour never carries a state
    // alone: pair it with text or a glyph.
    property color success: "#83e1ce"
    property color successContainer: "#164b40"
    property color warning: "#e0a84a"
    property color warningContainer: "#3a2a10"
    property color error: "#ffb4a8"
    property color errorContainer: "#4d2228"
    property color errorLink: "#ffd2d2"
    property color info: "#a8d4f5"
    property color infoContainer: "#1d3a52"
    property color infoLink: "#d6ecff"
    property color neutralContainer: "#263244"
}
