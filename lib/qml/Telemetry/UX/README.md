# Configurator view and provider contract

`Telemetry.UX 1.0` supplies `Configurator` and the independently embeddable
`FieldEditor`, `CommandPanel` and `ServicePanel`. All use one shared renderer.
It has no Telemetry SDK, transport, client, FIFO, Descriptor parser or snapshot
ownership. The host owns a non-null QObject provider and its models for the
entire view lifetime. The existing InspectorController is one dynamic provider. The optional
[StaticConfiguratorProvider](StaticConfigurator.md) borrows a generated typed
API through a separately generated binding and implements the same view contract.

```qml
import Telemetry.UX

Configurator {
    anchors.fill: parent
    provider: host.deviceProvider
}
```

`provider` is a required property. No `controller` context property or global
singleton is read. A host may place the view in its own page or tabs without
copying the view or editing generated files. Connection controls, discovery,
permission opt-in and device ownership remain host responsibilities.

Use a narrow panel in a host-owned tab without endpoint navigation:

```qml
FieldEditor {
    anchors.fill: parent
    provider: host.deviceProvider
    headerComponent: Component { Label { text: "Device configuration" } }
    footerComponent: Component { Label { text: "Application-owned help" } }
}
```

Import `QtQuick.Controls` for the example's `Label`. `CommandPanel` and
`ServicePanel` have the same `provider`, `headerComponent` and `footerComponent`
properties. The host selects an endpoint through its existing provider; mounting
a panel neither selects nor submits it. A panel hides its editor and actions
when the current selection belongs to another category. Several panels may
observe one provider, sharing its selection and busy state.

`Configurator` also accepts `headerComponent` and `footerComponent` for host
content around the complete view. Narrow panels display only their category's
operation history. `EndpointPanel` and `PlainToolTip` are implementation details;
applications should use the four public components above.

The provider exposes notifying properties:

| Properties | Contract |
| --- | --- |
| `endpoints`, `values`, `responses` | QAbstractItemModel objects owned by the host |
| `watchlist` | QAbstractListModel owned by the host |
| `selection`, `serviceResponseEndpoint` | QString display names |
| `selectedEndpointIndex` | Optional current `QModelIndex` in `endpoints`; invalid when none is selected. Enables selection highlighting in `EndpointTreePanel`. |
| `connected`, `busy`, `editing` | Connection/operation/draft state |
| `selectionRequiresDiscard` | Optional host guard for changed input, including invalid editor text. Supplied by both built-in providers; independent of `canApply`. |
| `canEdit`, `canApply`, `canRefresh` | Host-authorized Field actions |
| `commandSelected`, `canExecute`, `serviceSelected`, `canCallService` | Operation selection and action gates |
| `watchActive`, `watchPending`, `canAddWatch`, `watchCount` | Watchlist state and gates |
| `watchStatus`, `writeOutcome`, `commandOutcome`, `serviceOutcome`, `statusText` | Already-classified display text; the view does not infer execution certainty |
| `watchIndividual`, `watchIntervalMs` | Writable watch mode and polling interval (0 to 60000 ms; 0 reads continuously at low priority); the provider validates changes |
| `watchIntervalMinimumMs` | The smallest interval the provider accepts (0 here); the interval box starts there, at 1 for a provider that does not say |
| `previewsSupported` | Optional Dynamic Qt Session capability; shows the tree's Preview values control when available |
| `selectionInfo`, `definitions`, `definitionRootIndex` | Optional Dynamic metadata map and filtered definition tree for the selected endpoint before a value is read |

Actions retain the existing provider signatures:

- `selectEndpoint(QModelIndex)`, `refresh()`, `beginEdit()`, `cancelEdit()`,
  `apply()`, `executeCommand()` and `callService()`.
- `setScalarText(QModelIndex, QString) -> bool` takes a cell's text as it is
  typed. For a Field the draft is implicit (BA-4): the first valid change opens
  it, and a change that leaves it equal to the value read, with no invalid text
  left, closes it again. `beginEdit()` stays for hosts that open a draft
  themselves. Text that is no value leaves the row as it was and returns false.
- **The value cells (`ValueCell.qml`, BA-4).** Details shows every value in
  one cell component, a value until it is clicked or opened with F2 or Enter
  on its row, then an editor in place, one row tall: a text field, or a choice
  for a bool or an enum (with "Enter code…"). Edits are live. Enter keeps the
  value, Tab and Shift+Tab keep it and open the next or previous cell, Escape
  puts back the value the cell had when it was opened, and leaving with text
  that is no value puts back the last value. Ctrl+Enter applies the draft, or
  executes or calls. A cell opens only on a row whose `editable` role is true
  and never while changes are off or the provider is busy. While its text is
  no value the cell says what it takes, from the row's `takes` role ("Enter a
  number from 0 to 255.", "Enter a number.", "Enter true or false.", "Enter
  Idle, Run or Stop, or a code."); a float says what it encodes. Integers also
  take hex, the value's bits at its width (0xFF, 0x3F800000).
- **No Edit button.** A Field's values are editable while it has been read,
  is writable and changes are allowed (`ValueModel::setEditOffer`, which the
  Dynamic provider and the Inspector example keep up to date; the Static
  provider's `editable` role does the same). Cancel and the accent Apply show
  only while a draft exists; Read hides meanwhile. Execute and Call are the
  accent action of a Command and a Service.
- **A Field picked with changes allowed is read once** by Details, so its cells
  can be edited at once: one read per selection, none while another is
  pending, none for a Field already read. With changes off nothing is read.
- `addWatch() -> bool`, `removeWatch(int)`, `clearWatch()`, `refreshWatch()`,
  `startWatch()` and `stopWatch()`.
- **The Watchlist's two views (WL-2).** `watchRows` (a `WatchRows`, both
  providers) is the Watchlist as the panel shows it: the `watchlist` model's
  rows in a list, one per value with its whole path (`tree: false`, in the
  order added or, with `sorted`, by path A-Z with numbers in order), or in a
  tree grouped by path (`tree: true`): a group, the Field, its members and
  array elements, each value under the node it belongs to, only above
  something watched, in descriptor order (by `watchKey`).
  - Its rows carry the `watchlist` roles it finds by name (a role the source
    lacks reads as nothing) and its own: `sourceRow` (-1 for a node above
    values), `depth`, `branch`, `expanded`, `node` (`value`, `group`,
    `struct`, `array`, `element`), `leaves` and `nested` (a member or element
    inside a Field, whose Status and rw the Field row carries). A node's
    `name` is its path, `leafStart` where its own step starts, `typeText` from
    the source's optional `pathTypes` role, and `text`, while it is folded,
    the summary of its values (`{Gain 1.5, Enabled true}`).
  - `folded` holds the paths of the folded nodes; a fold outlives the rows
    under it, and adding a value opens the nodes above it.
    `setExpanded(row, bool)`, `expandAll()`, `collapseAll()`,
    `parentRow(row)`, `sourceRows(row)` (the values at and under a row),
    `reveal(sourceRow)` and `revealPath(path)` (the row, its parents opened), and
    `lines(row, paths)`, what a copy takes: one `path = value` line per value at and
    under the row as shown, or the paths alone. The panel copies with Ctrl+C (lines),
    Alt+Shift+C (paths) and its row menu (Copy, Copy value, Copy path); the Device
    tree copies a row's path with Ctrl+C and Alt+Shift+C as its menu's Copy path does.
  - A change of the Watchlist's rows changes only the rows it touches (the
    rows before and after are compared by path), so a row being edited keeps
    its editor while values come and go around it; a reading moves no row and
    tells the nodes above it once per event-loop turn. Switching the view or
    the sort resets the rows; the panel keeps the current value current.
  - The provider makes it, so a host needs no registered type for it. A
    provider without `watchRows` shows its `watchlist` in a list, with no
    List | Tree switch.
- **The number format (WL-3), Dynamic.** `watchFormat` (`"original"`, `"dec"`
  or `"hex"`) is how the Watchlist shows its numbers; a row may have its own
  (`setWatchRowFormat(QVariantList of rows, format) -> bool`, `""` to follow
  the Watchlist's). Formats are made from each reading's exact bits, never a
  new read: Original as the type means it (an enum by its name, `true`, f32
  `1.5`), Dec integers and codes in decimal (a bool `1`), Hex `0x`, uppercase,
  padded to the type's width, floats as their raw bits (`0x3FC00000`). Rows
  carry `shownText` (the value in the row's form) and `format` (the row's own,
  or `""`); `text` keeps its fixed form for recording and Trend. A row's own
  format is remembered by path, so a value removed and added again, after a
  reconnect or from a project, comes back in it: `watchRowFormats() ->
  QVariantMap` (`{"device.Config.Gain": "hex"}`) and `setWatchRowFormats(map)`
  are what a project saves. `writeWatch` takes hex whatever is shown
  (`0x4000_0000` writes 2.0 to an f32; leading zeros and `_` are allowed, a
  digit past the type's width is refused in words); a number's editor opens in
  hex on a row shown in hex. A provider without `watchFormat` shows its rows'
  `text` and no format controls.
- **The "+" row (WL-4b), Dynamic.** A path typed or pasted under the last row,
  as Live Expressions' "Add new expression". `checkWatchPath(text) ->
  QVariantMap` says what Enter would add, in words: `{status: adds, watched,
  missing, error or empty, message, adds, already, row}` ("Adds 21 values of
  device.Config", "device.Config.Gainn: did you mean Gain?", "control.Execute
  is a Command. Only Fields can be watched.", "device.Key is write only, so it
  can't be watched."). `watchPathCandidates(text) -> QVariantList` lists what
  may come next, at most 50 `{path, typeText, values, container, watched}`:
  groups, a group's Fields, a struct's members, an array's elements
  (`data.Samples[0 … 63]`), then paths whose last step holds the word typed.
  `addWatchPaths(QStringList) -> QVariantMap` adds what each line names - a
  path, a struct or array for all its values, or a line copied from the
  Watchlist (`path = value`, `path<Tab>value`) - as one change `undoWatch()`
  takes back: `{status, added, already, missing: [paths], refused: [{path,
  reason}]}`. The panel shows the row (`WatchAddRow.qml`) where a provider has
  all three; Insert, or Down from the last row, edits it; Tab completes the
  common prefix; several pasted lines are added at once.
- `removeWatchRows(QVariantList of rows) -> QVariantMap` (Dynamic) stops
  watching several rows as one change that `undoWatch()` takes back; the panel
  uses it for a node's × and Delete. `{status: ok, invalid or refused,
  changed}`. Without it the panel removes the rows one by one.
- `watchCandidate(QModelIndex) -> bool` tells whether an `endpoints` row could be
  watched, without selecting it. `addWatchMany(QVariantList of QModelIndex) ->
  QVariantMap` adds several Fields in one model change, leaving the selected
  endpoint and any open draft untouched and sending no request. The map holds
  `status` (`ok`, `refused`, `stale` or `invalid`) and the counts `added`,
  `duplicate`, `unsupported` and `capacity`.
- **The Device tree's watch boxes.** Both providers have them, Dynamic and Static
  alike:
  - `watchState(QModelIndex) -> int`:
    - `-1` when nothing at or under a row can be watched (a Command, a Service, a
      Field the Watchlist cannot hold, a stale index);
    - otherwise `Qt.Unchecked`, `Qt.PartiallyChecked` or `Qt.Checked` for none,
      some or all of the values under it.
    - In Dynamic, a value is a scalar Field or a scalar or enum inside a struct
      or array Field, at any depth (S3-A). A struct's or an array's box counts
      all its values, whether its rows are open or not; a member's, an
      element's and a range row's box counts its own. Opening a struct changes
      no box and keeps Undo.
  - `setWatchMany(QVariantList, bool watched) -> QVariantMap` watches, or stops
    watching, every value the Watchlist can hold at or under each index, in tree
    order, as one change and with no request sent. A value's row is named by its
    path: `device.Config.Gain`, `data.Samples[3]`.
    - The map holds `status` (`ok`, `refused`, `invalid`, `stale` or
      `capacity`) and `changed`, `unchanged`, `fields` (the values asked for),
      `room`; Dynamic adds `values` (a struct's or array's values were asked)
      and, when adding, `adding` (how many rows that would add).
    - All or nothing: a group that does not fit changes nothing, and `room` says
      how many more fit.
  - Dynamic only: `watchTally(QModelIndex) -> QVariantMap` is what a box counts,
    `{watched, total, kind}`, kind `members`, `elements`, `values` or `fields`,
    for the words of a partly ticked box ("2 of 3 members watched").
    `watchRefusal(QModelIndex)` says why a Field's box is off: write only, no
    values, larger than the Watchlist reads at once (64 KiB), or a connection
    that can't read struct members.
  - `undoWatch() -> QVariantMap` undoes the last such change: removed rows go
    back in their places. `canUndoWatch` says whether it can: nothing else may
    have changed the Watchlist or the tree since. In Dynamic it works while the
    Watchlist polls.
  - `watchRevision` moves whenever a box may show something else: the Fields
    held, the tree, or whether boxes can be answered at all (a model update, a
    new connection, the Watchlist's source). A view asks `watchState` again
    then. `watchLimit` is 8192 (1024 until WL-7).
  - Nothing is answered until it can be: without a current session, or while
    the Watchlist cannot tell which Fields it holds, `watchState` is `-1` and
    no empty answer is kept. A read into Details does not hold the boxes.
  - The first question after the tree changes builds `WatchCoverage`:
    - in Dynamic, one walk collects the Field ids, the Watchlist is asked once
      which of them it can hold (a scalar, or a struct's or array's number of
      values), and a second walk builds the coverage; it stops at every
      endpoint, so value rows are never walked;
    - in Static, one walk builds it.

    After that each question is a hash lookup, and a change of the Watchlist
    costs one pass over the tree. An index the walk did not see (a tree since
    replaced) is `stale`.

### Host-controlled endpoint activation

`EndpointTreePanel.selectionHandler` is an optional function receiving the source
`QModelIndex` for an explicit click, Enter/Return, search activation or **Show
details** action. With no handler, the tree keeps calling the provider directly.
Watch boxes and cursor navigation do not invoke this handler. A host can route
it through its own asynchronous discard decision and reveal an existing Details
panel after accepted selection, without creating another provider or client.

Pass the index immediately to an owner that validates it. A pending decision
must retain a persistent index and invalidate it on model/session changes;
keeping a plain index in JavaScript across a reset is insufficient.

The built-in providers report `selectionRequiresDiscard` even for invalid input.
Static conservatively reports it after editing back to an initial value. A fresh
same-endpoint selection preserves its draft; a stale or cancelled request can
still be explicitly prepared again. This property does not authorize an Apply,
Execute or Call, and the library does not show a discard dialog automatically.

For a host-owned prompt, `contextMenuVisible` and `contextMenuReturnFocus` expose
the menu's visibility and borrowed pre-menu focus item. Capture that item while
the menu is visible, before native popup activation; it becomes null on close.
Opening the prompt after the menu closes avoids competing focus owners. The
host remains responsible for lifetime checks, default Cancel and focus return.

### Dynamic tree previews and typed Details

The [device-tree contract](../../../../doc/ENDPOINT_PREVIEWS.md) describes the
Session-backed Dynamic presentation. `QtConnectionBinding::createProvider()`
attaches the existing Session's preview cache automatically; hosts do not create
another client, queue or owner loop. Providers without this optional capability,
including the generated Static provider, retain the existing metadata/text
fallback and do not show a background-preview toggle.

Selecting a Field exposes its wire type, access and byte size through
`selectionInfo`. `definitions` projects the selected branch of `endpoints`, with
`definitionRootIndex` locating its parent. Structure members and array ranges are
created locally from the schema. Expanding them sends no read and supplies no
invented payload. Details initially says **Not read**; Refresh performs the
explicit Field read. Command/Service metadata selection does not execute or call
the endpoint.

`EndpointTreePanel.previewValues` defaults to `false` (the owner, 14 November
2026: reading in the background is the user's choice); its toggle is available
only when `previewsSupported`. The view calls
`setPreviewDemand(QObject* view, QVariantList sourceIndexes)` with actual visible
rows and `clearPreviewDemand(QObject* view)` when demand ends. Collapsed
aggregates, hidden/offscreen rows and metadata search do not create future
demand. Children resolve to their owning Field ID, including ID zero, and are
neither endpoints nor Watch candidates. An accepted request may drain after a
view is hidden; its stale completion cannot warm a replacement lease/session.

The existing progress owner services previews after foreground, Watchlist and
Batch work, with one pending request, at least 500 ms between attempts and a
5,000 ms refresh age. The Session runs only when it has work, so its progress
takes replies in but is no clock: each cycle, and the once-a-second move of the
ages shown, starts from the previews' own single-shot timer, which runs only
while rows are in view. The shared cache holds at most 64 Fields and 256 KiB of
payload, with an 8 KiB per-Field limit. At most 16 view leases each contribute
256 IDs; 256 is not a global union limit. Local expansion adds at most 4,096
descendants per model, in pages of 128 and array ranges of 256. Limit/stale/error
states stay explicit. These bounds do not guarantee a sampling frequency.

Metadata-enabled Dynamic value models additionally expose `typeText`,
`editorKind`, `editorChoices`, `editorChoicesTruncated` and `canonicalText`.
Field, Command and Service use the same editor. Boolean labels are `false (0)` /
`true (1)`; enum labels combine name and exact decimal code. Choice `text` is
the canonical string supplied to `setScalarText`, never a JavaScript Number.
Up to 256 named enum choices are shown; **Enter code…** keeps unnamed or omitted
codes editable. A missing enum metadata cache uses the text fallback.

`canonicalText` is backend-formatted from the prepared typed value, separate
from the user's incomplete or invalid input. Finite F32/F64 readouts use 9/17
significant digits for round trip: entered `0.1` can remain visible while its
F32 caption says **Encoded: 0.100000001 (f32)**. It does not claim successful
transmission or device storage. Overflow/underflow refusals do not replace the
previous typed value or send a request; existing permission and Apply gates
remain authoritative.

With a Session-backed Dynamic provider, the Watchlist and the rest of the panel
work together. While it polls, selecting an endpoint, refreshing Details, editing,
Apply, Commands, Services and Batch all work, and so do the Watchlist's own
changes: ticking boxes, adding, removing, Undo and Clear (`watchChangesWhilePolling`
is true). A change enters the list at once; a read already sent finishes as asked
and is applied to the rows still there; the next portion is planned from the rows
shown then, so every cycle reads one list. An addition between two cycles is read
at once, a removal waits for the timer; an empty list polls nothing and says so.
Each `WatchFrame` carries the `membership` its read was planned from, so a recorder
files it under the list it asked for. The read mode and the interval still wait
for polling to stop. A provider without `watchChangesWhilePolling` (the Static
one) keeps its rows while it polls. An open draft
(a Field edit, or the request editor a selected Command or Service opens) does not
hold the Watchlist: a watch read never touches it. Requests interleave in the
session's one ordered FIFO: the Watchlist keeps at most one portion (up to 32
Fields) in it, so a write waits behind one exchange at most, and while the write
is in flight the Watchlist sends nothing. Afterwards the written Field is read
again at once if this cycle had already read it (or had it in flight); after a
Command, Service or Batch the next cycle follows immediately instead of waiting
for the timer. A write from another panel on the same session does not stop
it either: a polling Watchlist reads its list again right after the current
cycle, a stopped one marks its values stale. A start refused because an
operation is in flight leaves `watchStatus` saying so. `busy` is this view's own work (an operation, a Batch)
and leaves the Watchlist's reads out, so Cancel and Batch editing stay available
while it polls. Several Watchlists on one session take turns: each session
progress starts with the next panel, and a Batch is never refused because the
session is busy. One Watchlist holds at most 8192 rows
(`watchlistRowLimit`); past it `addWatchMany` counts the rest as `capacity`.

**Values inside struct and array Fields (S3-A).** A row is keyed by
`WatchKey` (`WatchKey.hpp`): a whole Field's key is its id; a value's is
`id | (1 + ordinal) << 32`, the ordinal its pre-order position among the
Field type's scalar and enum leaves (members in declaration order, elements in
index order, depth first; `WatchLeaves` counts, locates and names them). A
key holds under one descriptor fingerprint only; what is saved is a path.
A cycle reads whole Fields in ReadMany portions (or FieldReads) as before,
and every value watched in one struct or array Field with one FieldRead,
through the source's optional `readMembers(field, leaves, completion)`
(ordinals ascending, 1 to 8192; answered in `completion.members`). That read
is published as `WatchFrame`s of at most 32 rows of the same cycle and portion,
each row carrying `leaf` (0 for a whole Field, else 1 + its ordinal); a series
is `id | leaf << 32`. A Field read for its values is at most 64 KiB
(`workbenchSnapshotBytes`); one whose values are written from rows at most
4 KiB (`workbenchPayloadBytes`).

`watchBindings()` gives the rows as a project saves them, `{group, name}` or
`{group, name, member}` (member as `RuntimeSchema::resolve` reads it: `Gain`,
`[3]`, `Phase[2].Offset`), and `addWatchBindings(list)` puts such rows back in
one change: `{status, added, duplicate, capacity, missing: [positions]}`.

The generated Static provider uses Individual FieldRead cycles instead. Its open
Field or Command/Service request draft prevents Watchlist reads, foreground actions
wait for the current cycle, and an accepted mutation stops polling. It supports
0–60000 ms, with a 10 ms cooperative timer for continuous mode, and the same
8192-Field host limit. See the [Static provider contract](StaticConfigurator.md).

The view consumes `readFinished(bool published)` and model `modelReset()`
signals to restore the first visible expansion. Models support their normal Qt
index/fetchMore interface. Endpoint roles are `name`, `detail`, `isEndpoint`.
Value roles are `name`, `detail`, `value`, `isAggregate`, `childCount`,
`loadedCount`, `editable`, `editorText`. Responses use the same roles except
`editable`/`editorText`; watchlist rows expose `name`, `status`, `text` (ListView
supplies `index`). Scalar display/editor values are strings, including U64/S64
and enum codes; the view never converts them through JavaScript Number.

Watchlist rows also carry the facts of their last `Ok` reading, so a consumer
never has to parse display text:

| Role | Contract |
| --- | --- |
| `statusCode` | `int(WatchStatus)` from `WatchSample.hpp`: NotRead 0, Pending 1, Ok 2, Stale 3, Failed 4. Stable across providers, whose `status` wording differs |
| `sampleText` | Display text of the last Ok reading; empty otherwise |
| `sampleValue` | That reading as a `double` when it is exact; no value otherwise |
| `sampleExact` | `true` only for a finite number a double holds exactly: an integer beyond 2^53 − 1, NaN or infinity is `false` and is not plotted. Enums give their code |
| `sampleRevision`, `sampleEpoch` | Change counters: a new reading increments the revision; a new session or admission changes the epoch |
| `writable` | The row accepts writes, taken from the device's endpoint table when the row is added: a value inside a Field when that Field is writable and at most 4 KiB |
| `watchKey`, `member` | The row's `WatchKey` (qulonglong) and, for a value inside a Field, its member text |
| `writeScope` | A value's write: `{field: "Config", others: 20, kind: "members"/"elements"/"values"}`; empty for a whole Field |
| `writeRefusal` | Why the row is not written from the Watchlist, in words ("device.Table is larger than one write, so one member can't be written."); empty otherwise |
| `leafStart`, `access` | Where the name's last segment starts (the prefix is drawn muted: `device.Config.` before `Gain`), and the row family's access mark, `rw` or `r-` |

The Session-backed Dynamic provider's optional `writeWatch(row, text)` writes a
watched Field from its row, as Details would: its own read, an exact draft of
`text`, the write and a confirming read. It runs on a
second view of the provider, so it never moves the Details selection or touches
its draft (the Details snapshot is marked stale, as after any write). It is
refused without permission (`canWriteWatch` false), for a read-only Field, or
while another row write is in flight (`watchWriting`). `watchWriteOutcome` says
what happened in words (confirmed, written but unconfirmed, an invalid text, the
device's refusal), `watchWriteFinished(confirmed)` signals the end, and the
Watchlist reads the row again at once: within the running cycle, or as a Read
once when it is not polling. Start during a Read once turns that cycle into the
first one of polling. A value inside a struct or array Field is written by
writing its whole Field: the Field is read, the value changed in it, and the
whole Field written and read back; the other values keep what was read (a
change the device made to them in between is overwritten). The outcome says
what was written and what reads back: "Wrote device.Config.Gain = 2 · read
back 2.", "…; the device reads back 2.5." or "…; it could not be read back.".
The row's editor says what Write all covers.
The Static provider does not expose this method or `canWriteWatch`; the shared
view therefore leaves its rows read-only.

The shared `WatchlistPanel` draws the row family's columns (Name, Value, Type,
rw, Status, ×): Value, Type and Status are as wide as the widest text they have
shown and only grow until the list is cleared or the zoom changes; as the room
for names shrinks, rw hides, Type narrows then hides, and Status becomes a mark
whose word is in its tooltip. A good reading is its value alone (no dot); a
reading not made yet is "–". A row is one line except while it is edited.

Adding, group-adding and removing Fields inserts or removes rows (only Clear
resets the model), so a persistent index — a view's current row, a Trend's
selection — keeps following its Field. Each row has its own index identity: an
index kept from before a removal reads nothing rather than another row's data.

`TrendPanel` plots one watched Field from those roles. It needs `provider` and a
host-created `TrendHistory` as `history` (making it creatable from QML is planned).
The history holds at most 512 points stamped with host presentation time, not
device time; it never starts, stops or issues a read, and records a gap when the
Field goes stale, fails, changes session or cannot be plotted exactly.

All model/provider display strings and tooltip details are rendered as plain
text. Names or enum labels containing markup remain literal data and cannot
request embedded images. A host that needs rich content supplies it in its own
custom panel rather than embedding HTML in these model roles.

The host serializes actions and model notifications on its Qt owner thread,
keeps the provider alive while the view or its deferred updates exist, and
implements all lifecycle/permission/error rules. Buttons forward their action to that provider; text can also be revalidated
on focus loss. The view supplies no automatic mutation replay or exactly-once
guarantee. Existing lazy array fetching remains limited to 128 immediate rows.

## Theme and Type

Two singletons in `Telemetry.UX` give the panels, and the programs built on them, one
look:

- **`Theme`** holds colour roles. A role names what a colour does — `surface`,
  `textMuted`, `outline`, `focusRing`, `warning` with `warningContainer` — not how it
  looks.
- **`Type`** holds the type scale and the row and control heights.

The defaults are a dark set. [tests/theme_contrast](../../../../tests/theme_contrast)
checks every text pair at 4.5:1 or more and every outline and focus ring at 3:1 or
more (WCAG 2.2).

A host restyles everything by assigning roles at startup; a later change reaches every
binding too:

```qml
import Telemetry.UX
Component.onCompleted: Theme.accent = "#4fa3ff"
```

**The type scale.** `Type` takes its base from the application font, so a host sets the
body size once in C++ before the engine loads. `Qt.application.font` does not notify
later changes.

```cpp
QFont font = QGuiApplication::font();
// The compact desktop base: 10 pt (13.3 px at 96 dpi), following a larger system font.
const qreal systemPoints = font.pointSizeF() > 0 ? font.pointSizeF() : 9.0;
font.setPointSizeF(std::max(10.0, systemPoints * 10.0 / 9.0));
QGuiApplication::setFont(font);  // menus, docks and every panel follow
```

Its steps are:
- `Type.caption`: the body divided by 1.125;
- `Type.heading`: the body times 1.125²;
- `Type.body`: the body itself.

Sizes are in points, which follow display scaling; the base follows the system font.
Whether Windows' separate *Text size* setting reaches Qt is not established.

`Type.fixedFamily` is the fixed-width family for values and codes. Its default is each
platform's own: Consolas, Menlo or DejaVu Sans Mono, because `"monospace"` can fall back
to a proportional face. QML cannot read the system's fixed font, so a host that wants
exactly that sets it once from
`QFontDatabase::systemFont(QFontDatabase::FixedFont).family()`.

**Rules for panels:**
- Use at most three steps on one screen.
- Use roles, never raw colours.
- Never let colour alone carry a state; add text or a glyph.
- `textMuted` and `textSecondary` are close (about 1.2:1 against each other): never let
  them alone carry a distinction. A section label is `textSecondary` in the heading
  weight; descriptions and times are `textMuted` in the body weight.
- On a selected row, all of the row's text is `selectedForeground`; muted or secondary
  text is not drawn on `stateSelected`, and none on `buttonFill`.
- Draw a focus ring 3 px outside its control. Its contrast is then measured against
  the surface around the control, not the control's own fill: the ring is the accent
  colour, which a primary button is filled with.

**Names.** `Theme` and `Type` are short. A host with a type of the same name imports
the module under a qualifier, `import Telemetry.UX as UX`, and writes `UX.Theme.accent`.

**Another body size.** `Type.body` defaults to the application font. A host whose
controls take their font from `qtquickcontrols2.conf`, rather than from
`QGuiApplication::setFont`, assigns `Type.body` once; `caption` and `heading` follow.
For live zoom, change `Type.body` and the host controls' font together. Rows,
controls, icons and spacing follow the body; assigning those derived size tokens
directly breaks their bindings. The client provides remembered 80–200% zoom as a
host feature; the library supplies the scale, not a settings store or shortcuts.

The panels in this module already use these colour roles and size tokens.

## Event history and current problems

Hosts can own a `LogModel`, a `LogFilterModel` over it, and a `ProblemsModel`
that writes new problems and their resolution to that log. These Qt models run
on the GUI thread and own no transport. `LogModel` retains at most 2000 events;
`ProblemsModel` holds current problems by stable key, with errors before warnings.
Repeated `raise()` updates a problem without adding a duplicate log entry.
Clearing the log does not resolve problems.

`SessionProblems` in the optional `qt_connection` component adapts a session's
connection failures, files-only limitation and identity problem. Applications
should not log the same session failure independently. Transfer and application
problems are separate adapters owned by the host.

For a Save dialog, capture `log.text(filter.sourceRows())` before opening the
dialog, then pass that string to `saveText(path, capturedText)` after acceptance.
Keeping numeric row indices while the dialog is open is unsafe because new
events enter at row zero. `saveTo()` is for an immediate export of the current
rows. The host owns the file dialog and decides which models its panels show.

## Device files

`DeviceFilesPanel` shows the device's files as a tree of their own paths - the
device's names, never renamed - with path-based selection, Open, Download and the running
transfers, each with a `ProgressLine`. The host owns the models, the dialogs and
every write to disk:

- `DeviceFilesModel` (C++, the `qt_connection` component) lists and states the
  files of a `ConnectionSession` on its own queue, with or without telemetry
  (a files-only connection lists them too), and refreshes on every new
  connection.
- `DeviceFileTransfers` (C++, the same component) downloads one file at a time
  with the SDK's `FileReader`, writes it through `QSaveFile` so a cancelled or
  failed download never leaves a truncated file under the real name, and reports
  progress, rate and time left in the designer's words. While a transfer runs
  it republishes the row on a 250 ms timer of its own, so the rate and "waiting
  for the device…" stay current while the session, waiting on the device,
  publishes nothing. It owns its readers: one whose request is still in flight
  is handed to the session when the model goes.
- `upload(path, fileIndex, localFile, deviceFolder, overwrite, viewer)` writes a
  saved copy inside the connected device's folder back to the device, in the
  same queue and rows (`UploadRole`, states `Writing` and `Written`, the
  running step in `StepRole`: `Check`, `Write`, `Save`, `Verify`):
  1. the index must still name the path; the device's file is read and
     compared with the copy's record of that path (`DeviceBytes::matches`) -
     changed since it was read, or never read here, and nothing is written
     unless `overwrite`; another size is refused;
  2. the copy is written whole with the SDK's `FileWriter`;
  3. it is saved when the device keeps files (Savable), or the row says it is
     kept until the device restarts;
  4. it is read back: those bytes become the record, marked as written
     (`DeviceBytes::keepBytes`), and the viewer's reference
     (`FileContent::refreshReference`); a byte the device keeps differently is
     counted. The copy is never changed.

  `OutcomeRole` says how it ended, for a row's buttons and colour: `Checked`,
  `ReadBackDiffers` and `NotChecked` (written); `Changed`, `NotKnown`,
  `SizeDiffers` and `NotWritable` (nothing written); `Partial` (stopped part
  way: the device's file may be part old, part new, and the record is
  forgotten); `Failed` (nothing written: the connection, a read, a cancel
  before writing). `retry(row, true)` writes it anyway after `Changed`,
  `NotKnown` or `Partial`; plain `retry(row)` tries again after `Failed` or
  `NotChecked`. A cancel waits for the request in flight and never starts the
  next step. The check and the write are not one step on the device: the
  protocol has no lock. Live is not paused here; a host pauses the file's
  `DeviceFileLive` while its row is active, and saves edits first - the job
  writes the copy on disk.
- The panel asks for a destination with `downloadRequested(selection)` - a save
  dialog for one file, a folder for several, kept below by the device's folders
  (`DeviceFileTransfers.localPathFor`) - for a viewer with `openRequested(file)`,
  and for the folder of a saved file with `showInFolderRequested(localPath)`.
  `localFileOf` and `urlOfLocalFile` turn a QML dialog's URL into the local path
  `download()` takes and back.
- Both C++ models need a build with the `qt_connection` component
  (`TELEMETRY_UX_BUILD_QT_CONNECTION`), which links the SDK's session; the panel
  itself is in the configurator module.

`headerShown` defaults to `true`. Set it to `false` when embedding the panel in
a host Explorer that provides its own section header, Download and Refresh
actions. This hides only the panel's header: selection, keyboard shortcuts,
context menus and request signals still work. `transfersShown` independently
controls the embedded transfer list; set it to `false` when the host shows
`DeviceTransfersPanel` in a separate dock.

### Recording Watch completions

`DynamicConfiguratorProvider::watchCompleted(const WatchFrame&)` publishes one
owned terminal result for an accepted Watch request, after native progress has
returned and before row notifications. The Qt connection adapter freezes types
and source identity before submission and copies canonical scalar/enum bytes
inside the native callback. No schema or RX view is borrowed by the frame.

A frame contains at most 32 requested IDs, including when an aggregate failure
returned no rows. Native aggregate, per-item and value statuses remain distinct.
An unavailable value has no raw bytes; it is never converted into zero. Sequence,
cycle and portion identify requests, not simultaneous device measurements.
Completion UTC and monotonic timestamps are host receipt times.

`Current` means the completion belongs to the current source at publication;
it does not promise every item succeeded or that a receiver kept the UI current.
`Failed` and `Drained` must not be published as fresh measurements. The captured
source snapshot remains meaningful after live admission retires. `sourceBinding`
is a provider-local source attachment identity, not the native admission epoch.
`exact` describes typed capture availability, separately from successful data.

A recorder consumes this signal directly on the owner thread and performs only
a bounded enqueue. Stop is allowed in the receiver; recursive polling/start is
refused during publication. A queued connection per frame would create another
unbounded queue. The host keeps the provider/session alive through accepted-work
drain, then flushes its storage before switching project or closing. Destroying
an arbitrary provider does not independently guarantee recording durability.
The first implementation covers Dynamic Watch sources; Static Watch capture
is not provided. SDK/UX acquire no SQL dependency or second device FIFO.

`WatchlistPanel.startHandler(polling)` and `stopHandler()` optionally delegate
explicit actions to a host workflow. `operationActive` keeps Stop available while
that workflow prepares or flushes. Without handlers the panel calls the existing
provider methods directly. Neither adding Fields nor installing a handler starts
polling by itself.

### Closing a file view

`OpenFiles.close(row)` announces `FileContent.retiring` before removing its row.
`FileViewer` then detaches pending Inspector delegates, prepares its codec view
and stops the existing Live reader. An accepted read can still finish through
the session's normal drain; closing a view neither cancels the shared session
nor starts another read. `FileContent.stopLive()` never creates a reader.
Session-backed Live requires the SDK's `FileReader::stopAfterCurrent()` API.
The accepted request keeps its normal deadline; a genuinely lost reply can still
require recovery.

A host that destroys a standalone `FileViewer` without closing its `OpenFiles`
row calls `viewer.prepareForUnload()` first. The same optional method on a custom
codec view lets it stop its timers and detach callbacks before its Loader clears
the context. Both preparation calls are idempotent. A hidden tab pauses and
resumes its existing view; replacing or closing it is a separate operation.
Returning from Hex to decoded Values creates a stopped view: Start is explicit.
The host still keeps the content/model alive until its viewers have released it.

The tree's rows are `DeviceFileRow` (the Basic style is imported there only):
a format icon, the device's name, size and `rws` access letters in columns shared
with the headings. Selection belongs to `DeviceFilesModel` and survives a refresh
by path. A folder represents its readable files and shows a count of those
selected. A known unreadable file can be selected, but Open and Download do not
send a read for it.
The current row is highlighted and framed while the tree has the keyboard;
Space marks, Enter opens a file or folds a folder, Left and Right fold and
unfold. A failed transfer is drawn in the error colour with Retry; a saved one
offers Show in folder; Cancel all and Clear finished sit by the Transfers
heading. `LinkButton` is the link-styled action they use.

`ProgressLine` is the one progress pattern: determinate for 0..1, indeterminate
for a negative value, shown only after 400 ms and kept at least 500 ms, in the
info colours, never the accent. It keeps its place in a layout while hidden and
fades in and out. See telemetry-qt-client `doc/DEVICE_FILES.md`.
