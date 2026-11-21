// One type label for every panel that shows a type (the designer's review of
// 20 November 2026, 14.2): a named type reads "adc::Config", its scope and
// "::" muted, its own name brighter, so the eye lands on Config and fifty
// repeated scopes read as texture. shortScopes cuts every scope to "…::"
// ("…::Config[4]", "(…::Reference) -> …::Offset") when a column gives its
// scopes way; a global "Config" stays as it is. A find's run, given on the
// full text, is underlined in the info tone, and a row matched by its type
// shows its scope whatever the column does. The text is escaped, never read
// as markup. Left to right in a mirrored layout: a type is code. MIT.
import QtQuick
import QtQuick.Controls

Label {
    id: typeLabel
    objectName: "typeLabel"
    // The full type text, as the model gives it.
    property string typeText: ""
    // Cut every scope to "…::" (the column's give-way step 3).
    property bool shortScopes: false
    // A find's run on the full text, {start, length}; empty for none.
    property var run: ({})
    property color scopeColor: Theme.textMuted
    property color nameColor: Theme.textSecondary
    property color runColor: Theme.info
    readonly property bool hasRun: !!run && run.length > 0 && run.start >= 0
                                   && run.start + run.length <= typeText.length
    // What is shown: the full text while a run is marked on it.
    readonly property string shown: shortScopes && !hasRun ? cut(typeText) : typeText
    readonly property bool cutShown: shown !== typeText

    // Every scope of a type text as "…::": a scope is a run of characters
    // other than ( ) , [ ] > and a space, ended by "::".
    function cut(text) {
        return text.replace(/[^\s(),\[\]>:]+::/g, "…::")
    }
    function escaped(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
    // The shown text in spans: scope (with its "::") muted, the rest in the
    // name's tone, the run underlined.
    readonly property string rich: {
        const text = shown
        const scope = new Array(text.length).fill(false)
        const pattern = /([^\s(),\[\]>:]+|…)::/g
        let found
        while ((found = pattern.exec(text)) !== null)
            for (let i = found.index; i < found.index + found[0].length; ++i)
                scope[i] = true
        const marked = new Array(text.length).fill(false)
        if (hasRun && shown === typeText)
            for (let i = run.start; i < run.start + run.length; ++i)
                marked[i] = true
        let out = ""
        let at = 0
        while (at < text.length) {
            let end = at
            while (end < text.length && scope[end] === scope[at] && marked[end] === marked[at])
                ++end
            const part = escaped(text.substring(at, end))
            const tone = marked[at] ? runColor : scope[at] ? scopeColor : nameColor
            out += marked[at] ? "<u><font color=\"" + tone + "\">" + part + "</font></u>"
                              : "<font color=\"" + tone + "\">" + part + "</font>"
            at = end
        }
        return out
    }
    LayoutMirroring.enabled: false
    textFormat: Text.StyledText
    text: rich
    color: nameColor
    elide: Text.ElideRight
    font.family: Type.fixedFamily
    font.pointSize: Type.caption
    Accessible.name: typeText
}
