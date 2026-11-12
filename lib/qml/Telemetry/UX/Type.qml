// The type scale and the sizes that follow from it, for the Telemetry panels and
// the programs built on them. MIT.
//
// A modular scale on the major second (1.125), the compact ratio for dense data.
// The base is the application font: a host sets it once with
// QGuiApplication::setFont before the first window loads (Qt.application.font
// does not notify later changes) and assigns `body` to zoom afterwards. Every
// panel and menu follows; the other steps and every size derive from it here.
// Points follow display scaling. Use at most three steps on one screen.
pragma Singleton
import QtQuick

QtObject {
    readonly property real ratio: 1.125
    // ms(0): rows, fields, buttons, menus, chips, sentences. By default the
    // application font in points; a pixel-sized one is converted at the primary
    // screen's logical density (96 dpi on Windows, 72 on macOS). A host whose
    // controls take their font elsewhere (qtquickcontrols2.conf) assigns it, and
    // caption and heading follow.
    property real body: Qt.application.font.pointSizeF > 0 ? Qt.application.font.pointSizeF
                      : Qt.application.font.pixelSize > 0 ? Qt.application.font.pixelSize * 72 / logicalDpi : 12
    readonly property real logicalDpi: Qt.application.screens.length > 0
                                       && Qt.application.screens[0].logicalPixelDensity > 0
                                       ? Qt.application.screens[0].logicalPixelDensity * 25.4 : 96
    // ms(-1): section labels in sentence case, times, descriptions, status bar,
    // column headers.
    readonly property real caption: body / ratio
    // ms(2): card titles and panel headings.
    readonly property real heading: body * ratio * ratio

    // Weights: DemiBold for headings, section labels and the primary action;
    // Regular elsewhere, never Bold inside running text. (Medium, 500, renders
    // as Regular with Segoe UI, which has no 500 face.)
    readonly property int headingWeight: Font.DemiBold
    readonly property int bodyWeight: Font.Normal

    // The fixed-width family for values and codes. QML cannot read the system's
    // fixed font, so a host may set it once, for example from
    // QFontDatabase::systemFont(QFontDatabase::FixedFont).family(). The default is
    // each platform's own: "monospace" can fall back to a proportional face.
    property string fixedFamily: Qt.platform.os === "windows" ? "Consolas"
                               : Qt.platform.os === "osx" || Qt.platform.os === "ios" ? "Menlo"
                               : "DejaVu Sans Mono"

    // The body in device-independent pixels: every size below is a multiple of
    // it, so a host that zooms by changing the body scales rows, controls and
    // room together, never the text alone (WINDOW_LAYOUT.md in the client).
    readonly property real bodyPixels: body * logicalDpi / 72

    // One height for rows, controls a little taller, chips a little shorter:
    // compact, as an IDE's (22 / 24 / 20 px at a 13.3 px body).
    property int rowHeight: Math.round(bodyPixels * 1.65)
    property int controlHeight: Math.round(bodyPixels * 1.8)
    property int chipHeight: Math.round(bodyPixels * 1.5)
    // Glyphs on buttons and in rows, and one level of a tree's indent.
    property int iconSize: Math.round(bodyPixels * 1.2)
    property int indent: iconSize
    // A check box: even, so it sits on whole pixels in a row (14 px).
    property int checkSize: 2 * Math.round(bodyPixels * 0.55)
    // Every control's corners.
    property int radius: 3

    // Room around and between things: inside a panel's edge, between the
    // items of one group, between groups, and the least that still separates
    // (6 / 4 / 8 / 3 px at a 13.3 px body).
    property int padding: Math.round(bodyPixels * 0.45)
    property int spacing: Math.round(bodyPixels * 0.3)
    property int groupSpacing: Math.round(bodyPixels * 0.6)
    property int tight: Math.max(2, Math.round(bodyPixels * 0.2))
}
