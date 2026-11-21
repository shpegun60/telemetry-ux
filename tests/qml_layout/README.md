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

`WatchLook.cpp` holds the Watchlist to its design of 20 November 2026 (WL-9,
section H, checks 1-15, 19 and 21) at 100 % and 125 %, over the fixture of its
section F - the demo device and the enum `device.Mode` - and a fake provider
that formats with the real `watch_format`, refuses Gain above 10, answers when
told and counts reads: rw on every row of a Field, words with their codes in
Original, Dec and Hex, a float's every digit in Dec, the folded summary, line 2
whole (its height from its laid-out lines, clear of the focus frame), the
commit rules with Enter refused and no trap, a refused or invalid text held
struck with what the device holds and one read after a refusal, the write's
feedback (the typed value, a progress line from 400 ms, one flash, the time
flashed), one last-action line, the pace in the bar or under it, the grid's
left line, the Name cap with × after the last column, the give-way, the kinds'
glyphs and guides, and the change mark (`watch-wl9-*.png`). `DetailsTree.cpp`
adds Details' refused Apply (H 18, `details-refused-560.png`), Enter refused in
a u8 cell (H 19), each row's kind glyph (the kinds design's J 10) and "-";
`DeviceTreeKinds.cpp` rw on Config's members (H 20).

The designer's review of 20 November 2026 (section 14, part A) is held by
four programs. `DeviceTreeGroups.cpp` checks the path grammar's helpers
(`group/…/endpoint.member[i]`, escapes, older dotted keys read against the
groups) and drives the Device tree over a model shaped as a group tree will be
- groups inside groups, `typeName` on some Fields: the opening rule (seven
top-level groups or fewer open one level, eight open nothing), subgroups first,
counts in words that give way from the right with "in N groups" in the
tooltip and the accessible name, one indent step for the whole tree (16 px at
420, 8 at 280, the guides straight), `*` and "Collapse all", the context line
(`power/metrology/harmonics` over the tree, a click on a segment, a
provider's request), find by path segments (`harm/H3`) and by named type with
the run underlined in the Type cell, the kinds filter, Copy path, and the path
label alone (the drop order, the leaf's elision, the keys of a breadcrumb)
(`device-tree-groups-*.png`). `DeviceTreeKinds.cpp` holds the one tree on the
demo device: no family rows, the Commands' and Services' `control` one folder
in declaration order, the funnel with the device's totals and "Commands only ·
3 matches", the context line (`device-tree-context-*.png`,
`device-tree-kinds-funnel-*.png`). `WatchLook.cpp` (22) holds the Watchlist's
path label, the Name cap's ceiling of 40 body heights and the tree's joined
chains (`watch-list-long-path-*.png`, `watch-tree-chains-560.png`), and
`DetailsLayout.cpp` Details' breadcrumb (`details-breadcrumb-*.png`).

PL-1 (the owner's screenshots of 21 November 2026: "measure/P…r" cut with the
Item column hundreds of pixels wide, and a path that lost its group while its
arguments stayed whole) is one rule, `PathLabelChecks.hpp`, held by every path
label's program: a path is whole whenever it fits its room as the label's own
fonts measure it (QFontMetricsF, 1 px tolerance), and otherwise only whole
segments drop, the fewest that fit, the leaf whole until nothing more can drop.
`DetailsLayout.cpp` runs it on the Batch's rows from 300 to 1,200 px and back,
side by side with Details and stacked, the pointer on each row and off, with
the cell's give-way order (the arguments down to `Type.nameFloor`, then whole
segments, then the arguments hide) and the row's … and × keeping their place
(`batch-paths-760-hover.png`, `batch-paths-1200-hover.png`), and on Details'
title; `WatchLook.cpp` on the Watchlist's List; `DeviceTreeGroups.cpp` on the
context line, and on labels exactly as wide as their implicit width, the
Batch's old sizing that showed the fault. The Log shows no path label yet
(LG-1 brings one).

`PathCompletion.cpp` holds WL-10a (the owner's screenshot: picking a group in
the Watchlist's "+" wrote "measure/.") through the real provider of a TCP
connection to the demo peer serving a group tree: in the Watchlist's and the
Batch's "+", by the mouse and the keys, a group gives "measure/" and lists what
it holds, a subgroup the same one level down (`demo/adc/channel1`), an endpoint
its whole path, "." and "[" only after a struct's or an array's Field or
member, and a member and an element complete and are added
(`path-completion-*.png`). The demo peer has no Batch: the Batch's row is given
the real provider's candidates and words with "unsupported" read as "adds".

`LogLines.cpp` draws the Log's lines (LG-1) as a host's list does, a
`LogLine` in an `ItemDelegate` over a real `LogModel` and `ProblemsModel`: a
Command run is one line with its kind word, the arrow, `control/Reset` and
"executed", no glyph and quieter words; a refused write one line with the
error glyph in the error colour, `device/Config.Gain`, "= 345" and the reason
in the primary colour; a Service its request and answer, "(2, 3) → 6"; the
grouped warning one line whose current row opens its list (20 paths, then
"and 12 more") and its resolution a line saying what changed. At 900 px
nothing is cut and nothing overlaps; at 320 px the source column goes (the
accessible name keeps it), the leaf stays whole while the words elide, and
the current row's long words go under the path, whole. A line changed in
place (a pending become its outcome) shows the new words in the same row
(`log-lines-900.png`, `log-lines-320.png`, `log-lines-320-current.png`).
