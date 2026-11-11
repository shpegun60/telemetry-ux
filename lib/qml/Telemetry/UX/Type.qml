// The type scale and the sizes that follow from it, for the Telemetry panels and
// the programs built on them. MIT.
//
// A modular scale on the major second (1.125), the compact ratio for dense data.
// The base is the application font: a host sets it once with
// QGuiApplication::setFont before the first window loads (Qt.application.font
// does not notify later changes), as max(12 pt, the system font x 4/3): a 16 px
// body, or larger when the system font is larger. Every panel and menu follows;
// the other steps derive from it here. Points follow display scaling. Use at most
// three steps on one screen.
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

    // Weights: Medium for headings, section labels and the primary action;
    // Regular elsewhere, never Bold inside running text.
    readonly property int headingWeight: Font.Medium
    readonly property int bodyWeight: Font.Normal

    // The fixed-width family for values and codes. QML cannot read the system's
    // fixed font, so a host may set it once, for example from
    // QFontDatabase::systemFont(QFontDatabase::FixedFont).family(). The default is
    // each platform's own: "monospace" can fall back to a proportional face.
    property string fixedFamily: Qt.platform.os === "windows" ? "Consolas"
                               : Qt.platform.os === "osx" || Qt.platform.os === "ios" ? "Menlo"
                               : "DejaVu Sans Mono"

    // One height for rows and controls, a little less for chips.
    property int rowHeight: 32
    property int controlHeight: 32
    property int chipHeight: 30
}
