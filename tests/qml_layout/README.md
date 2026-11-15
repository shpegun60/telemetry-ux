# Configurator layout regression

This test embeds the real Configurator with a disconnected SDK ConnectionSession
and QtConnectionBinding. It performs no network or device operations. The UX
QML module is built from the current source; the SDK is consumed from an explicit
installed prefix. It does not qualify protocol behavior or physical hardware.

```sh
python3 -B tests/qml_layout/run.py --build-dir /external/layout-check \
  --sdk-prefix /installed/telemetry-sdk --qt-prefix /usr \
  --cxx g++ --cmake cmake --ninja ninja
```

The real QQuickWindow embeds the Configurator in a ColumnLayout and is resized
to 1160×800 and 820×620. Collapsed and expanded
Watchlist/Batch panels must leave usable endpoint and value trees. The test checks
scroll reach, keyboard focus visibility, an actual mouse toggle, separate Batch
response/empty geometry, a reachable custom footer, and removal of optional host
slots/panels. QML warnings fail the test. Screenshots show collapsed, expanded top,
and expanded bottom states at both sizes. Source, installed SDK, tools, executable
and image hashes are retained with the result. Use a fresh external output directory.

The outer workspace scrolls vertically once the main pane's 400 px minimum and
the advanced panels exceed the viewport; inner trees/lists retain their existing
scrolling. Hiding panels or supplying host header/footer components changes only
presentation and does not change provider ownership or its operation scheduling.

The standalone 320 × 240 Watchlist also checks wrapped controls, an exact U64
value and long error status, the visible scrollbar, window-delivered synthetic wheel events, Tab focus,
and a 65-item virtualized list without issuing reads or starting polling. Widened
to 720 px, the same list becomes one 28 px table row per Field: the value aligned
under its column title, `Ok` a dot with the status word hidden, a failure in words
under the Status title and a long one cut to one line, the small remove button
inside its row (`wide-watchlist.png`); `collapsible: false` hides the expand box. Rows
can have different heights when text wraps; as with Qt ListView in general, the
scrollbar's size and position are estimates until more rows have been laid out.

`TreeActions.cpp` drives the tree's row actions over a nested fixture (groups,
Fields, Commands) and a provider that records what it is asked to watch: Watch
and its duplicate, Watch this group across nesting in tree order, a group with
nothing to watch, Show details on a Command, Copy path through the clipboard,
the Menu key and Shift+F10, the actions refused while polling, and `find()` with
Enter (a Field is watched, a Command opens its details) and the arrow keys
between the search field and its results.

`ValueCell.cpp` drives the value cell Details and the Batch share (BA-4) alone,
over fake rows: a value until clicked, hovered with its pencil in the gap after
it, one row tall open or not, live text, the type's words only for text that is
no value (and the row a caption taller then), Enter and leaving with no value
putting the last value back, Escape the value it had when opened, Tab and
Shift+Tab asking for the next and previous cell, a bool's and an enum's choice
as wide as its items with "Enter code…", a float's Encoded caption, and a locked
or read-only cell that never opens (`value-cell-*.png`).

`EndpointStates.qml` also gives Details a Field's read values (BA-4·0b): at
480, 360 and 280 px every row is one row tall, striped with `surfaceAlternate`,
Value and Type at one x on every row, nothing scrolls sideways, Type hides
before Name is narrower than 96 px, and line 2 and the status line say "Field"
and "Read", then "Edited, not applied" with a draft (`details-family-*.png`).

`UndoKeys.cpp` holds the central Undo's keys to one press, one step (UR-1): a
window with the real Configurator over a disconnected Session, Details as a host
holds them (`UndoScope.refusal`), a file in a `FileViewer`, a plain button and
text field, an inert area, a menu and a modal dialog, one `UndoRouter`. With the
keys on every place the Tab key reaches and every list, two steps are recorded
and Ctrl+Z pressed once: exactly one step is taken and said once, a text field
keeps the keys, the inert area does nothing and Details only say why. Then the
irreversible note said by the first press, Ctrl+Y and Ctrl+Shift+Z, the Edit
menu acting from Details, a notice that speaks for a step (`noticed`) and one
hidden, a blocked step's words on `UndoButtons`, the toolbar never taking the
keys, a long path cut for a menu, the file's history taken once the keys are in
its Viewer and the workspace's once it records later, the file's steps waiting
while it is written, a menu or a modal dialog taking no step and leaving
`active` as it was, and a dock floated out of the window (a window it owns)
where Ctrl+Z still takes a step and an inert area in it does nothing. `WatchTree.cpp` and `Bulk.cpp` record into a real
`UndoHistory` through `ScriptSteps.hpp` (the fixture's JavaScript as a step's
target) with a router in their windows, and check the notices' `undoNoticeLink`
(Undo, then Redo), Clear and removal with no question, and a tree with
`watchBoxes: false` offering no watching.
