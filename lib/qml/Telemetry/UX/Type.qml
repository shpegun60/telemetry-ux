// The type scale and the sizes that follow from it, for the Telemetry panels and
// the programs built on them. MIT.
//
// A modular scale on the major second (1.125), the compact ratio for dense data.
// The base is the application font: a host sets it once with
// QGuiApplication::setFont (a 16 px body is 12 pt), and every panel and menu
// follows (before the first window loads: Qt.application.font does not notify);
// the other steps derive from it here. Sizes are in points, so the
// system's text size still enlarges them. Use at most three steps on one screen.
pragma Singleton
import QtQuick

QtObject {
    readonly property real ratio: 1.125
    // ms(0): rows, fields, buttons, menus, chips, sentences.
    readonly property real body: Qt.application.font.pointSizeF > 0 ? Qt.application.font.pointSizeF : 12
    // ms(-1): section labels in sentence case, times, descriptions, status bar,
    // column headers.
    readonly property real caption: body / ratio
    // ms(2): card titles and panel headings.
    readonly property real heading: body * ratio * ratio

    // Weights: Medium for headings, section labels and the primary action;
    // Regular elsewhere, never Bold inside running text.
    readonly property int headingWeight: Font.Medium
    readonly property int bodyWeight: Font.Normal

    // One height for rows and controls, a little less for chips.
    property int rowHeight: 32
    property int controlHeight: 32
    property int chipHeight: 30
}
