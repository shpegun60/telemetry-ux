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
| `canEdit`, `canApply`, `canRefresh` | Host-authorized Field actions |
| `commandSelected`, `canExecute`, `serviceSelected`, `canCallService` | Operation selection and action gates |
| `watchActive`, `watchPending`, `canAddWatch`, `watchCount` | Watchlist state and gates |
| `watchStatus`, `writeOutcome`, `commandOutcome`, `serviceOutcome`, `statusText` | Already-classified display text; the view does not infer execution certainty |
| `watchIndividual`, `watchIntervalMs` | Writable watch mode and polling interval (0 to 60000 ms; 0 reads continuously at low priority); the provider validates changes |
| `watchIntervalMinimumMs` | The smallest interval the provider accepts (0 here); the interval box starts there, at 1 for a provider that does not say |

Actions retain the existing provider signatures:

- `selectEndpoint(QModelIndex)`, `refresh()`, `beginEdit()`, `cancelEdit()`,
  `apply()`, `executeCommand()` and `callService()`.
- `setScalarText(QModelIndex, QString) -> bool` validates raw editor text. The
  view preserves invalid/intermediate text and uses the result to gate its input.
- `addWatch() -> bool`, `removeWatch(int)`, `clearWatch()`, `refreshWatch()`,
  `startWatch()` and `stopWatch()`.
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
      some or all of the Fields under it.
  - `setWatchMany(QVariantList, bool watched) -> QVariantMap` watches, or stops
    watching, every Field the Watchlist can hold at or under each index, in tree
    order, as one change and with no request sent.
    - The map holds `status` (`ok`, `refused`, `invalid`, `stale` or
      `capacity`) and `changed`, `unchanged`, `fields`, `room`.
    - All or nothing: a group that does not fit changes nothing, and `room` says
      how many more fit.
  - `undoWatch() -> QVariantMap` undoes the last such change: removed rows go
    back in their places. `canUndoWatch` says whether it can: nothing else may
    have changed the Watchlist or the tree since, and the Watchlist must not be
    polling.
  - `watchRevision` moves whenever a box may show something else: the Fields
    held, the tree, or whether boxes can be answered at all (a model update, a
    new connection, the Watchlist's source). A view asks `watchState` again
    then. `watchLimit` is 1024.
  - Nothing is answered until it can be: without a current session, or while
    the Watchlist cannot tell which Fields it holds, `watchState` is `-1` and
    no empty answer is kept. A read into Details does not hold the boxes.
  - The first question after the tree changes builds `WatchCoverage`:
    - in Dynamic, one walk collects the Field ids, the Watchlist is asked once
      which of them it can hold, and a second walk builds the coverage;
    - in Static, one walk builds it.

    After that each question is a hash lookup, and a change of the Watchlist
    costs one pass over the tree. An index the walk did not see (a tree since
    replaced) is `stale`.

With a Session-backed Dynamic provider, the Watchlist and the rest of the panel
work together. While it polls, selecting an endpoint, refreshing Details, editing,
Apply, Commands, Services and Batch all work; only adding or removing rows waits
for polling to stop. An open draft
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
session is busy. One Watchlist holds at most 1024 Fields
(`watchlistRowLimit`); past it `addWatchMany` counts the rest as `capacity`.

The generated Static provider uses Individual FieldRead cycles instead. Its open
Field or Command/Service request draft prevents Watchlist reads, foreground actions
wait for the current cycle, and an accepted mutation stops polling. It supports
0–60000 ms, with a 10 ms cooperative timer for continuous mode, and the same
1024-Field host limit. See the [Static provider contract](StaticConfigurator.md).

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
| `writable` | The Field accepts writes, taken from the device's endpoint table when the row is added |

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
first one of polling.
The Static provider does not expose this method or `canWriteWatch`; the shared
view therefore leaves its rows read-only.

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
  progress, rate and time left in the designer's words. It owns its readers: one
  whose request is still in flight is handed to the session when the model goes.
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
