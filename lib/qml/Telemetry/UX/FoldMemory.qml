// What a user opened in a value tree, kept by path: per (scope, section), the
// paths opened or folded and the current row's path. A Batch pane keeps one
// for as long as the Batch panel exists, scoped by item; Details keeps one per
// selected endpoint. Pure QML. MIT.
import QtQml

QtObject {
    id: memory

    // "scope|section" -> { open: { path: bool }, current: string or null }.
    // The current row is only ever one the user made current (a click, a
    // key): null until then, and "" is a Field's root row.
    property var store: ({})

    function slot(scope, section) {
        const key = scope + "|" + section
        if (store[key] === undefined)
            store[key] = { open: {}, current: null }
        return store[key]
    }
    // Whether anything is remembered for the table.
    function has(scope, section) {
        const found = store[scope + "|" + section]
        return found !== undefined && (Object.keys(found.open).length > 0 || found.current !== null)
    }
    function note(scope, section, path, open) {
        slot(scope, section).open[path] = open
    }
    function noteCurrent(scope, section, path) {
        slot(scope, section).current = path
    }
    // Whether the user made a row of the table current.
    function hasCurrent(scope, section) {
        const found = store[scope + "|" + section]
        return found !== undefined && found.current !== null
    }
    function currentPath(scope, section) {
        const found = store[scope + "|" + section]
        return found === undefined || found.current === null ? "" : found.current
    }
    // The remembered paths, shallowest first (a range before the elements it
    // holds), as [{path, open}].
    function entries(scope, section) {
        const found = store[scope + "|" + section]
        if (found === undefined)
            return []
        const depthOf = function(path) {
            const steps = path.length === 0 ? 0 : (path.match(/[^.\[\]]+|\[[^\]]+\]/g) || []).length
            return steps * 2 + (path.indexOf("…") >= 0 ? 0 : 1)
        }
        return Object.keys(found.open).map(function(path) { return { path: path, open: found.open[path] } })
            .sort(function(a, b) { return depthOf(a.path) - depthOf(b.path) })
    }
    function isUnder(path, root) {
        return root.length === 0 || path === root || path.indexOf(root + ".") === 0 || path.indexOf(root + "[") === 0
    }
    // Forgets the folds under a path (and its own), "" for the table.
    function forgetUnder(scope, section, path) {
        const found = store[scope + "|" + section]
        if (found === undefined)
            return
        for (const key of Object.keys(found.open))
            if (isUnder(key, path))
                delete found.open[key]
    }
    function forgetTable(scope, section) {
        delete store[scope + "|" + section]
    }
    function forget(scope) {
        for (const key of Object.keys(store))
            if (key.indexOf(scope + "|") === 0)
                delete store[key]
    }
    function clear() {
        store = ({})
    }
}
