# Reusable Qt/QML Configurator

`Telemetry.UX.Configurator` displays a caller-provided model and explicit actions.
It creates no client, transport, execution queue, timer, snapshot or session.
The [provider contract](Telemetry/UX/README.md) describes the required presentation
properties and methods. The existing Inspector controller is one provider; a user
can supply another QObject with that contract.

Build only the view with Qt 6.12 or newer, without an SDK installation:

```sh
cmake -S . -B /external/telemetry-ux-view -G Ninja \
  -DTELEMETRY_UX_BUILD_INSPECTOR=OFF -DTELEMETRY_UX_BUILD_CONFIGURATOR=ON \
  -DCMAKE_PREFIX_PATH=/path/to/qt -DCMAKE_INSTALL_PREFIX=/external/ux-prefix
cmake --build /external/telemetry-ux-view --target install --parallel 2
```

In a separate Qt application, add the installed prefix to `CMAKE_PREFIX_PATH`:

```cmake
find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2)
find_package(TelemetryUX 0.1.0 CONFIG REQUIRED COMPONENTS configurator)
target_link_libraries(my_app PRIVATE TelemetryUX::configurator Qt6::QuickControls2)
```

```qml
import Telemetry.UX

Configurator {
    anchors.fill: parent
    provider: host.deviceProvider
}
```

The module is static and directly linked. It requires no plugin import macro,
runtime environment variable, manual engine import path or Telemetry SDK provider.
Qt resource objects are part of the installed CMake target. The QML source and
`qmldir` are also installed for tooling; `TelemetryUX_QML_IMPORT_PATH` identifies
that tooling directory when needed by an IDE. The resource import works even in
an executable that loads its main QML file from disk instead of declaring
`QML_FILES` to CMake.

`NO_CACHEGEN` keeps the installed view free of Qt private QML-cache ABI dependencies.
The QML engine compiles the view normally when it loads it. This package contains
native Qt resource objects, so use a matching compiler/platform/Qt kit or rebuild
the package for that kit. The Python code-generator distribution is separate and
does not require building or installing this Qt module.

The Inspector builds this same source module when configured on its own. A root
build with Inspector enabled includes the required view even when the standalone
view option is disabled. Disabling both options performs no Qt or SDK discovery.

For a custom page, embed `FieldEditor`, `CommandPanel` or `ServicePanel` instead
of the full Configurator. They use the same renderer and host-owned provider,
with no endpoint browser or automatic request. All four views accept optional
`headerComponent`/`footerComponent` extensions; see the provider contract above.

A host that builds its own workspace can embed the same controls individually:

```qml
import QtQuick
import QtQuick.Controls
import Telemetry.UX

SplitView {
    anchors.fill: parent
    EndpointTreePanel { provider: host.deviceProvider; SplitView.preferredWidth: 300 }
    EndpointPanel { provider: host.deviceProvider; SplitView.fillWidth: true }
    WatchlistPanel { provider: host.deviceProvider; SplitView.preferredWidth: 360 }
    BatchPanel { provider: host.deviceProvider; SplitView.preferredWidth: 360 }
}
```

The example expects a provider supporting Watchlist and Batch. Hosts that accept
other provider types must check those capabilities before instantiating the
corresponding panel, as `Configurator` does. The application owns the provider
and session and must keep them alive while any panel uses them. Sharing a
provider also shares its selected endpoint, edit draft, Watchlist and Batch; it
does not create a client, a second FIFO, or a polling timer in the view. Hiding
or showing a Watchlist panel does not stop or restart polling. If the application
wants to stop polling, it must request that explicitly via the provider. A new
provider has its own presentation and polling state and must be chosen deliberately.

`WatchlistPanel` shows its rows when `expanded` is true; its title (a chevron,
"Watchlist" and the count) folds them. A host that gives the Watchlist a dock of
its own sets `collapsible: false`: the rows are then always shown and the title
is hidden, and `framed: false` drops the card. One toolbar row stays above the
rows - Read once, Start, "Every" and the interval, a ⋯ menu with Watch the
selected Field, the two read modes and Clear the Watchlist… - and gives way as
the panel narrows: "Every" first, then Read once moves into ⋯. The interval is a
list of steps with Other… for any interval in milliseconds. The polling status
is a wrapped line under the toolbar; a host whose status bar already says it
sets `statusShown: false`, and advice the status bar would cut short (the device
cannot read with ReadMany and FieldRead is not chosen) still shows there.

From about 360 px wide (`tableRows`) every Field is one row under Name, Value and
Status titles that stay put while the rows scroll: the value right-aligned in the
fixed-width font, `Ok` as a green dot, any other status in a word (Refused,
Stale, Timed out…; the provider's own word is its tooltip), and × on the row
under the pointer or the current row, clear of the scrollbar. Narrower panels
give a Field two lines - its name and ×, then its value and status - each a row
high. From the keyboard the arrows move the current row, Delete removes it and
F2 writes a writable value. Every size follows `Type`.

Removed with the one-row toolbar (no host in these repositories used them): the
scroll-anchoring properties (`scrollAnchor`, `scrollOffset`,
`headerImplicitHeight`, `seenHeaderHeight`, `adjustingScroll`) and the object
names `addWatchButton`, `clearWatchButton`, `watchMode`, `watchFrequency`,
`watchIntervalHint`, `watchSettings`, `watchActions` and `watchlistScroll`; their
actions are `addWatchItem`, `clearWatchItem`, `watchModeReadMany` /
`watchModeFieldRead` and `watchInterval` / `watchIntervalInput`.

A writable Field is changed in its own row, as in Live Expressions: double-click
the value (or press the pencil that appears on hover), type, and Enter writes; Esc
or clicking elsewhere cancels, so nothing is written by accident. The editor opens
with the exact number of the last reading. The outcome of the write shows under
the toolbar in words. Rows are writable when the provider gives them a `writable`
role and `canWriteWatch` is true; a provider without them shows no editor.

`EndpointTreePanel` includes case-insensitive search by endpoint name or path
(for example, `measure.Voltage`). Results display their full path; choosing one
passes its original model index to the provider. Searching only examines the
existing model and never requests a value or changes the selected endpoint.
Clear the search to return to the tree. Search scans incrementally and displays
at most the first 500 matches, with a depth limit of 64 levels; the status says
when traversal is limited, even if there are no additional matches. Refine the
query to narrow the results.

**Watch boxes.** With a provider that has `watchState` and `setWatchMany` (both
the library's providers do), a box means "watched", at once:
- **Fields and groups.** A Field's box watches that Field. A group's or a
  category's box watches every Field under it the Watchlist can hold. It shows
  all, some or none (✓, –, empty).
- **Clicking and Space.** A click or Space on the current row watches all when
  the box shows some or none, and stops watching all when it shows all.
- **Rows without a box.** A row with nothing that can be watched has no box but
  keeps its slot, so names line up. A group says so in its tooltip.
- **The status line.** One reserved line at the bottom, never wrapping, says
  what the last change did: "Added 65 · Undo", "Removed 69 · Undo",
  "No room: 1,500 Fields, 524 free". Its tooltip gives the whole sentence.
- **While the Watchlist polls.** The boxes are off and the line says "Stop
  polling first".
- **Older providers.** A provider without these methods keeps the older ticked
  marks and **Add N**.

The tree has no heading of its own (its dock names it) and no standing
footnote.
- **Rows** are `Type.rowHeight` tall with a 16 px indent, and are not striped.
- **The search** has its clear button inside the field.
- **Inside a dock**, a host sets `framed: false` for a plain surface edge to edge.
- **Starting closed:** a host that leaves every branch for the user to open sets
  `openOnLoad: false`.

Every row and search result has actions on a right click, the Menu key or
Shift+F10: **Watch** (one Field; **Stop watching** when it is watched), **Watch
this group** (every Field under a group, nested, in tree order; at most 4096 rows
are visited; with watch boxes **Watch all in group** or **Stop watching group**,
as the box shows), **Watch all Fields** (the
same over the whole tree; `watchAll()` for a host's own menu), **Show details**
(selects the endpoint, as a click does) and **Copy path** (the dotted path, for
example `measure.Voltage`). Watching goes straight to the provider's `addWatchMany` and
leaves the ticked marks alone; while the Watchlist polls, the Watch actions are
disabled (Enter in the search says to stop polling first). In the search, Enter
watches the first result (a Command, Service or Field the Watchlist cannot hold
opens its details instead), the status says which, an Enter pressed before the
search finishes acts when it does, and Down walks into the results. With no
result, Enter is left to the host. A host binds its own
Ctrl+K to `find(text)`, which focuses the search, optionally with a query.

Results contain copied display names. If an external session invalidation does
not immediately notify the view, already displayed names may remain until the
provider resets or updates its model; selection still goes through the provider's
normal validation. Hosts should notify/reset their models when their schema or
session changes.
