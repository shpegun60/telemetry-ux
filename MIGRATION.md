# Generator source provenance

The QML emitters were moved from `shpegun60/telemetry`, revision
`4d7afbb48476b5e73c179105f89d6f639bd7bf9c`, directory
`tools/telemetry_codegen/generators`, into `python/telemetry_ux_codegen`.
The original copyright and MIT license are retained.

| Original file | Original Git blob |
| --- | --- |
| `qml.py` | `56ebb57bab3641502d70fe69dc1d5e2bf3a52631` |
| `qml_client.py` | `fc97a7b961cbc8a57ad881a7d7fde4027af6f255` |
| `qml_js.py` | `be353918f9bef717038f4e4c473e79b96ba13dca` |
| `qml_macros.py` | `0e40ba0df649542401781aba47cca67c41aa6e56` |
| `qml_rpc.py` | `0bc06f441c8830d1e585230d51a6b41736c124de` |
| `qml_values.py` | `5b89505bf40dbe7111c2c9d8fdd74cd957776971` |

The initial move changes private SDK helper imports into imports from the
versioned public `emitter_api`. It does not rewrite generated Qt classes,
conversion rules, pending bookkeeping, RPC integration or wire formats.
There is one QML emitter implementation. Parsing, SDK generation and guarded
publication remain owned by the SDK.

The SDK and UX are separate repositories and packages. Matching exact
development versions and emitter API/IR versions are checked at generation.
Installed consumers must not depend on a sibling checkout or a source fallback.

## Inspector application

The single Inspector application and its QML/TCP suites move from SDK revision
`3a66a10f7742ac6ddaaef0f181a09bfc71b7d82e` to `examples/inspector` and the two
matching `tests` directories here. The old SDK implementation is removed by its
coordinated migration PR; no forwarding application or second controller is
retained. The executable name, QML URI and operation/lifecycle behavior remain
unchanged. Only the former disabled-UART diagnostic changes to name the installed
transport component.

The maintained Inspector now lives under `examples/inspector`, alongside the
other optional consumers of the UX libraries. Root library builds leave it
disabled unless `TELEMETRY_UX_BUILD_INSPECTOR=ON`; configuring its own CMake
project or running its Windows packager still builds the same example.

The normal application consumes an installed SDK. QML/TCP tests record the
actual installed headers/libraries and SDK metadata separately from their UX
sources. SDK-side serial GUI tests remain an explicit cross-repository gate
because their PTY peer uses the actual NUCLEO firmware Protocol and private
COBS profile. Those firmware fixtures are not copied into UX or required by a
normal application build. Historical hardware receipts retain their original
paths, revisions and bytes; this move makes no new physical-board claim.

## Reusable QML view

`lib/qml/Telemetry/UX/Configurator.qml` extracts the Inspector's existing endpoint,
value, editor, Service response and Watchlist presentation. The application keeps
its connection controls and supplies the same controller explicitly. Both hosts
use the same view; no second client, queue, snapshot store or schema engine is
introduced. Existing object names and user action routing are preserved.

The static `TelemetryUX::configurator` package installs the QML view, public Qt
dependencies and native resource objects. Its independent package gate hides the
producer source/build tree, relocates the prefix and loads the view from a plain
Qt host with a user-owned provider. This verifies view packaging, not generated
typed-client integration or new physical hardware behavior.

The Widgets playground and other example UI consumers have separate migration
gates. This view extraction does not claim those later migrations are complete.

## Browser presentation and starter

The browser provider and renderer are UX-owned ES modules. Static mode imports
the generated JavaScript SDK and structural metadata; dynamic mode uses the
SDK's admitted DynamicClient and validated runtime schema. Both supply one
presentation contract to the same renderer, retaining the host's existing
client, FIFO and transport ownership. No Descriptor parser, payload codec or
retry engine is copied into UX.

The optional `generate_web_starter` extension uses the same frozen SDK schema
and guarded CLI publication as the QML emitters. It generates three thin entry
files, not another form implementation. Installed-package browser tests exercise
both actual clients against a persistent native Model and Encoded dispatcher;
they do not establish a physical-device, Ethernet or firmware HTTP port.

## Generated QML example and qualification

The generated-QObject example and its QML qualification suite originate in SDK
revision `6e15220b1271139b3f1996a16d385c660f856f45`, directories
`examples/codegen_qml` and `tests/codegen_qml`. Their new application-owned
`examples/codegen_qml/fixture` retains the demo DTOs and endpoint definitions from
the SDK's codegen and structured-client fixtures, with the original MIT notices.
The SDK's headless fixtures and codec tests remain SDK-owned.

The example and tests link installed SDK targets rather than compiling SDK
implementation files. Generation uses matching installed core and UX packages;
the test-only Descriptor builders are fixtures, not another runtime parser.
Encoded and RPC modes keep their existing generated client adapter and one FIFO.
This ownership move changes build and test inputs, not transport, generated API
semantics or wire formats. Removal of the former SDK presentation copies follows
qualification of this separate UX consumer.

## Structured payload-file tools

The former SDK `examples/structured_client` browser page and HTTP helper now
belong to UX, together with their browser interaction checks. They originate in
SDK `6e15220b1271139b3f1996a16d385c660f856f45`. The application-owned DTOs and
native producer retain their MIT notices; portable SDK runtime and headless
codec/native interoperability tests remain SDK-owned.

The browser imports the installed `telemetry-sdk/runtime` export. The native
helper links installed `core` and `resource_adapter` targets. The localhost
adapter serves a fixed set of UI/runtime/fixture routes without exposing either
source checkout. Its existing one-process-per-call demonstration contract and
manual payload-file workflow are preserved; this tool does not create a second
SDK client or replace the standard Configurator. SDK presentation-copy cleanup
follows qualification of the installed UX consumer.

## Standard HTML element

`defineConfiguratorElement()` optionally registers `<telemetry-configurator>`.
It delegates to the existing `mountConfigurator` renderer and accepts the same
host-supplied provider. The element introduces no other form implementation,
client, transport, polling timer or execution queue. Registration is explicit;
importing the package does not require a DOM or modify a custom-element registry.
Connection, removal and provider replacement manage only the view subscription.

## Opt-in typed QML Configurator

`--ux-starter qml` requires explicit `qml` and structural metadata and adds a
small generated `ConfiguratorBinding.hpp`. It enables the C++ wire-preserving
codec helper and generated raw-cache hooks only for this profile. Existing
ordinary QML, C++, JS/TS and web-starter profiles retain their defaults.

The installed Qt package exports `TelemetryUX::static_configurator` in addition
to its existing provider-neutral view. It borrows the application's generated
API; the existing dynamic Inspector remains a separate provider of that same
view. The static provider supports an explicit Individual FieldRead Watchlist
through the same generated API/FIFO. It exposes no ReadMany capability. Hosts
can hide polling controls with `watchlistVisible: false`.

## Shared headless WebSocket carrier

The borrowed WebSocket carrier now lives in the SDK under
`telemetry-sdk/transports/websocket/carrier`. Existing
`telemetry-ux/transports/websocket` imports re-export that implementation and its
types. Upgrade the pinned SDK/UX pair together; a previous development tarball
with the same `0.1.0` label may not have the new export. Browser import maps and
deployed assets must include the SDK carrier module. The reference demo and
assets exporter do so without pulling in the optional owned Session coordinator.
There is no change to borrowed socket ownership, frame handling or execution.

## Qt 6.12 and the compact type scale (November 2026)

Hosts of the `Telemetry.UX` module see these changes. The package still says
`0.1.0`; take the SDK/UX pair together.

**Qt 6.12 LTS is required.** CMake must be 3.25 or newer.

**Resource prefix.** The module now sits under the standard `/qt/qml` prefix. A
host that loads a file by URL must write `qrc:/qt/qml/Telemetry/UX/...` where it
wrote `qrc:/qt-project.org/imports/Telemetry/UX/...`. A codec that reuses a UX
view is one such host. Importing the module by name is unchanged.

**Sizes follow the body.**
- `Type` derives every size from `body` in pixels: rows, controls, chips,
  icons, the check box and the room. `body` is the application font, at least
  10 pt.
- To zoom, assign `Type.body`.
- Do not assign the size tokens themselves: an assigned token stops following
  the zoom.

**EndpointPanel.**
- With nothing selected it shows one line, and no heading or actions.
- `framed: false` drops the card.
- The footnote below the values is gone. Refresh's tooltip and a caption note
  for a draft, a Command or a Service carry its text.

**DeviceFilesPanel.**
- The transfer list moved into `DeviceTransfersPanel`. It is shown under the
  files while `transfersShown` is true, which is the default, and is not made
  at all otherwise.
- `deviceFilesActions` and `deviceFilesMarked` are gone. The count and the
  marked size are in `deviceFilesSummary`.
- `deviceFilesDownload` and `deviceFilesRefresh` are icon buttons now. Their
  `text` is the action's name, which shows as the tooltip.

**The Device tree.** The box at the start of a row is `RowCheckBox`, shared
with Device files. Its tooltip opens after 700 ms.

**Generated QML API on Qt 6.12.** Qt reads a JavaScript argument's getters
while it converts the argument.
- A getter that throws: the generated method still runs and refuses the
  incomplete value as `Validation`, and the exception then reaches the QML
  caller. No request leaves.
- A getter that changes the session: it runs before the method looks. The
  refusal is `Schema` instead of `Validation`, and again nothing is sent.

## Watchlist keys (S3-A, November 2026)

The Watchlist holds values inside struct and array Fields. C++ consumers of the
installed headers see these source changes; QML and the provider's invokables
only gain members.

| Before | Now |
| --- | --- |
| `WatchlistModel::Removed{row, id, name}` | `Removed{row, entry}`, `entry` a `WatchEntry` (`key`, `name`, `member`, `type`, write scope) |
| `WatchlistModel::removeIds(QSet<quint32>)`, `DynamicWatchlist::removeMany(QSet<quint32>)` | take `QSet<WatchKey>`; a whole Field's key is its id |
| `DynamicWatchlist::held() -> QSet<quint32>` | `QSet<WatchKey>` |
| `DynamicWatchlist::holdable(ids) -> std::optional<QSet<quint32>>` | `std::optional<QHash<quint32, WatchHolding>>`: `{aggregate, leaves}` per Field |
| `setWritable(std::function<bool(quint32)>)` | `std::function<bool(WatchKey)>`; `setWriteRefusal` added |
| `WatchCoverage::Holdable{key, name}`, `build(model, holdableOf)` | adds `leaves` and `aggregate`; `build` takes an optional descend predicate |
| `WatchFrameRow`, `WatchlistCompletion::Row` | gain `leaf`, last, so positional initialisers keep their meaning |

New: `WatchKey.hpp`, `WatchLeaves.hpp`, `WatchlistSource::readMembers`,
`WatchlistCompletion::members`, `DynamicWatchlist::addEntries`, `entries`,
`resolve`, `binding`, `leaf`, `readsMembers`; `EndpointModel` roles `path`,
`watchLeafFirst`, `watchLeafCount`, `watchLeafKind` and `pathOf`;
`WatchlistModel` roles `watchKey`, `member`, `writeScope`, `writeRefusal`.
A FieldRead `WatchFrame` may now hold up to 32 values of one Field.

The Watchlist's rows in the row family's columns (WL-2a) add only the
`WatchlistModel` roles `leafStart` and `access`; a host's own Watchlist model
without them shows the whole name in one colour and no rw column.

## The Watchlist as a list or a tree (WL-2b, November 2026)

The Watchlist panel shows the provider's `watchRows` (`WatchRows.hpp`, new in
the base target) instead of `watchlist`, and its rows' `index` is no longer
the `watchlist` row: the panel passes `sourceRow` to `removeWatch` and
`writeWatch`, so a provider's calls keep their meaning. Both providers gain
`watchRows`; Dynamic gains `removeWatchRows`. A host's own provider without
`watchRows` keeps the list it had, without the List | Tree switch. A host
test that finds a Watchlist row by `index` finds it in the order shown: in
the list as added, the same as before.

The panel's row is a `FocusScope` (it was an `Item`), so an open editor keeps
the focus when the list gives the row the focus again.

## Typed paths and the "+" row (WL-4b, November 2026)

Additive only. `WatchLeaves` gains `range`, `children` and `typeText`;
`DynamicWatchlist` gains `resolvePath` and `pathCandidates`; the Dynamic
provider gains `checkWatchPath`, `watchPathCandidates` and `addWatchPaths`.
`WatchAddRow.qml` is new in the QML module. With a provider that has the three
invokables the Watchlist's list is as tall as its rows, the "+" row right under
the last one; without them it fills its room as before.

## Rows for paths a device lacks; the Watchlist kept across a disconnect (WL-4c, November 2026)

One change of behaviour. `DynamicWatchlist::retire(true)`, and so a disconnect
or a source replaced, no longer empties the Watchlist: every row stays with its
path and its last value (as it was shown, in its format), status `Stale`, or no
status if it was never read - with no device a row says nothing of devices.
Such a row has an unresolved key (`watch_key::unresolved`, bit 52 set, never a
real key): it is never read, written or recorded, has no access mark, type or
editor, and `held()` leaves it out. Code that reads `WatchlistModel::key(row)`
skips `watch_key::isUnresolved` keys; the low word of one is not an endpoint
id. When a device attaches, every such row is found again by its path, in
place: a struct kept whole becomes its values, minus those a row above already
names, or claims none of them when they do not fit in the 1024 rows. A path the
device lacks stays a row with status `Missing` ("Not on device" in the panel);
one on it that names nothing to watch (a Command, a name two Fields share, no
room) has status `Unwatchable` ("Can't watch"), the reason in `writeRefusal`.
Both map to the new `WatchStatus::Missing` (5). A host test that expected
`count() == 0` after a retire now finds the rows; `clear()` still empties, and
removing rows needs no device any more. `WatchRows` places a row again when its
key changes in place (a dataChanged with no roles listed), so the tree keeps
descriptor order through a disconnect.

`addWatchBindings` keeps a binding this device has no value for as such a row
too (once; `added` counts values, `missing` gives the positions, and a binding
with no group is reported and kept nowhere, since it could not be saved back).

Additive: `watch_key::unresolved` and `isUnresolved`; `WatchlistModel`
`unbindAll`, `rebindRow`, `markMissing`, `isUnbound`, `hasBound`;
`DynamicWatchlist` `missingEntry`, `unboundRow`, `rebindRows`, `readable`; the
Dynamic provider's `watchReadable` property and `restoreWatchBindings(bindings)`
- a saved Watchlist back in its order as one change, refused before anything
is cleared when it could not be done. `addWatchPaths` keeps a path the device
lacks as a row too (`added` counts values only; `missing` names the kept paths,
and a path kept already counts in `already`), and `checkWatchPath` says
`missing` with Enter allowed; Enter on a half-typed path the suggestions go on
from takes the first suggestion instead. `watchBindings` answers without a
device as well, from the rows' paths. With only such rows Start and Read once
are off, and the status says there is nothing on this device to read.

## Adding to the Batch by name or path (BA-1, November 2026)

`WatchAddRow.qml` is now a thin adapter over `PathAddRow.qml`, a "+" row any
panel gives its own words and rules; its names, signals and behaviour are
unchanged. `BatchAddRow.qml` is the Batch's, under the Batch's list in
`BatchPanel.qml` when the provider has the calls below.

Additive: `EndpointModel::endpointList()` (every endpoint as an
`EndpointEntry`, in the tree's order) and `endpointIndex(category, id)`;
`DynamicBatch::refusal(rows)` (`Refusal`: None, Busy, Session, Previous,
Permission, Items, Bytes), `add(std::vector<BatchRow>)` - all rows in one
change or none - and `itemLimit()`; the Dynamic provider's `checkBatchPath`,
`batchPathCandidates`, `addBatchPaths` and `selectBatchPath`. One change of
behaviour: `DynamicBatch::canAdd(row)` is now `refusal` of that row alone, so
a Batch that still holds items of an earlier connection, which are never sent
again, takes no new item until it is cleared.

## The Watchlist's number format (WL-3, November 2026)

`WatchlistModel` gains the roles `shownText` and `format`, `setFormat`,
`setRowFormat`, `pathFormats`, `setPathFormats` (at most `formatLimit` paths)
and `valueType`; a row's own format is kept by path, so `WatchEntry` is
unchanged. `WatchFormat.hpp` is new in the dynamic
target. The panel shows `shownText` where a row has it, else `text`; `text`
itself is unchanged, so recordings and Trend read what they read before.
`DynamicConfiguratorProvider::writeWatch` now accepts `0x…` and writes its
exact bits, or refuses in words (a digit past the type's width, a NaN whose
payload a write cannot keep); any other text is taken as before.

WL-3b adds `watch_format::encode(text, type)`, which returns the bits a typed
value would write or the reason it cannot (out of range with the type's
bounds, not a number, a sign or exponent the type does not take), and
`watch_format::range(type)`. `DynamicConfiguratorProvider::watchEditPreview(row,
text)` returns `{preview, error}` for the row being edited: the panel shows
`= 0x40000000` under the field while the text is a value, the reason in place
of it while it is not, and Write stays off until it is.

*Reset number formats to Original* - in the "…" menu, a row's *Number
format* menu and the narrow format menu - calls the new
`DynamicConfiguratorProvider::resetWatchFormats()`: the Watchlist's format
back to Original and every row's own cleared, those kept for paths not
watched now included, as one change in the provider's undo slot, which
`undoWatch()` takes back while no format has changed since (from any
panel, or a project loaded). `watchFormatsDiffer()` says whether anything
is shown other than as Original, for the items' enabled state.
`watchRevision()` now also moves with every format change. In the panel,
the notice line goes, Undo with it, once the revision moves, as the
Device tree's does, and Ctrl+Z works from anywhere in the panel but a text
field. A provider without `resetWatchFormats` does not get the items.

## Several Watchlist rows selected (WL-2c, November 2026)

`WatchRows` keeps a selection by path: `selectedCount`, the `selected` role,
`select(row, toggle)`, `selectRange(row, add)`, `selectAll()`,
`clearSelection()`, `anchorRow()`, `selectedRows()`, `selectedSourceRows()`
(every value at and under the selected rows, once, in the order shown) and
`selectedLines(paths)`. A row that goes, or that a fold hides, leaves the
selection; folds that hide all of it select the nodes they fold.
`beginChanges()`/`endChanges()` (and `WatchRows::Changes`) hold the rows while
one call changes many source rows and bring them up to date once; in between
the rows answer nothing. The dynamic provider holds them around its bulk
changes, a reconnect and a project's Watchlist put back.

`DynamicConfiguratorProvider::chooseWatchFormat(format)` is the Watchlist's
format as the panel's user chooses it, one change `undoWatch()` takes back;
the `watchFormat` property stays a project's or host's, with no Undo of its
own. `setWatchRowFormat` is one undoable change for all the rows given.

In the panel the keys move and select (Shift extends, Ctrl moves the cursor
alone, Ctrl+Space, Ctrl+A, Escape), clicks select with Ctrl and Shift, and
Delete, Ctrl+C and Number format act on the selection when the current row
is in a selection of two or more. Removing several values with no Undo to
bring them back (no device, or a provider without `removeWatchRows`) asks
first. A provider without `WatchRows` keeps the one current row.

The list sorts by name from the Name header or *Sort by name* in the "…"
menu: `WatchRows::setSorted` now moves the rows into place instead of
resetting them, so a row being edited keeps its delegate. The tree keeps the
device's order. A node's name shows a tooltip with its path, type and the
values watched; × names what it removes; rows tell a screen reader what a
member or element belongs to and whether a node is expandable and expanded
(Qt 6.12's `Accessible.expandable`/`expanded`), and the selection's size is
announced once it settles.

## A mark for rows in a format of their own (WL-3c, November 2026)

`DynamicConfiguratorProvider::watchOwnFormatCount` counts the watched rows
shown in a format of their own other than the Watchlist's, counted once per
change of the formats or the rows; the reset's notice counts the same rows. The
panel shows it as a small mark before the format control in both its shapes
("● 3"), whose press shows every value as Original (with Undo). Where a
provider has the count the mark is always in the row, only clear and out of
reach while it counts nothing, so its coming and going moves nothing. Past
`WatchlistModel::formatLimit`, the formats of paths not watched now give way
oldest first (by when each was set), no longer in hash order; formats put back
together (`setPathFormats`, an Undo) keep when each held path was set, and the
paths new to the map count as set in path order.

## The read plan on its own (WL-6, November 2026)

`ReadPortions.hpp` (installed with the dynamic configurator) holds the plan
the Watchlist builds at the start of each cycle: `read_portions::plan` takes the
watched keys in row order and what the source can do, and returns the portions
(ReadMany of whole scalars within the advertised budgets, one member read per
struct or array Field, one FieldRead each in Individual mode) with a failure
code where it stops; the Watchlist puts the code in the words it always used.
Nothing it sends changed. The Device tree's previews (PV-2) are to plan with
it next.

## Struct Fields read in one Batch (WL-6, November 2026)

Where the device has Batch, the Watchlist reads the values it watches in two
or more struct or array Fields with one Batch of FieldReads instead of one
read each, within the Batch's item, request (and the host's 4096-byte buffer)
and reply budgets; a Field too large for any Batch, of unknown size, or left
alone stays a read of its own, and Individual mode reads each on its own as
before. `WatchlistSource` gains `batchCapability`, `valueBytes` and
`readBatch` (all optional; the Qt connection binding supplies them), and
`WatchlistCompletion` gains `parts`. Each Field of a Batch is published as
frames of its own with `WatchOperation::Batch` (new, 3) and that item's own
outcome, so **a frame observer that tells FieldRead from ReadMany must accept
Batch** - a recorder storing the operation in particular. A refused Field
shows why in its rows and the cycle goes on; a Batch that failed as a whole
stops the Watchlist as any failed read does. A written struct of a Batch is
read again on its own.

## The Watchlist says its pace (WL-6, November 2026)

`DynamicConfiguratorProvider::watchPace` (from `DynamicWatchlist::pace()`)
says how polling keeps up with its interval: what the last cycle read and with
which exchanges, its time, the median of the last five cycles and periods, the
ticks skipped, and the period polling achieves - a whole number of intervals,
since a tick due while a cycle runs is skipped. From the first completed cycle
the Watchlist panel shows a line in place of the status ("A cycle of 65 values
takes 410 ms"; behind, "Every 2 s, not 1 s: a cycle of 65 values takes 1.2 s"),
with the exchanges in its tooltip, and says falling behind and catching up to a
screen reader. **The line shows even where a host sets `statusShown: false`**,
since it corrects the interval the toolbar shows; a wait, a failure or a stop
still shows only with the status. A host that shows the polling period
elsewhere should take `watchPace.achievedPeriodMs`. A Read once ends with
"65 values read in 150 ms".

## Device tree previews in one snapshot a cycle (PV-2, November 2026)

`EndpointPreviews` no longer reads one Field every 500 ms. A cycle reads every
Field in view in as few requests as the device allows - scalars by ReadMany,
structs, arrays and scalars without ReadMany whole in a Batch of FieldReads, a
FieldRead each without either - one request at a time while the Session is
idle, and the values land together when the last reply is in. The next cycle
starts an interval after the last landed (`interval()`, `setInterval()`: 2 s by
default, 1 to 10 s); `readNow()`, or a Field in view without a value, reads
sooner but never within 500 ms of the last cycle; a value is stale after three
intervals, at least 5 s, and a failed read keeps the last value, stale, with its
reason. `EndpointPreviewSource` gains the optional `readMany`, `readBatch` and
their budgets (the Qt connection binding supplies them), and `read_portions`
gains `wholeReads`. `refreshMs` and `spacingMs` are gone.

The Device tree's "Preview values" check box is now a menu button, "Values:
off" or "Values: every 2 s", offering Off, Every 1 s, Every 2 s, Every 5 s and
Read now. `EndpointTreePanel.previewValues` still turns previews on and off;
`previewIntervalMs` (2000 by default) is new, and a host saves both with its
project. The provider gains `previewIntervalMs` (one value for every tree on a
connection) and `readPreviewsNow()`.

## The Watchlist's row editor says more (WL-5, November 2026)

While a row is edited and the Watchlist polls, the live reading stands beside
the name ("Now 1.48"; a failing read its status word, in the warning colour).
`DynamicConfiguratorProvider::watchWriteKind` says what the last write's
outcome is - `writing`, `confirmed`, `differs`, `unconfirmed`, `refused` - and
the panel shows a confirmed write quietly and the others in the primary text
colour. F2, or a double-click, on a value that can't be written says why: read
only, not connected, changes off (with Allow changes beside it), a write in
progress, or the provider's own reason.

## A Command's or Service's values in the Batch row (BA-2, November 2026)

The Batch's "+" row takes a Command's or Service's values after its path, named:
`control.Scale value=2 factor=3`. `request_arguments` (new,
`RequestArguments.hpp`) reads that text against the request type, checks each
value as Details would and, when every one is valid, encodes the request.
`EndpointPaths::Pick` gains `tail` and `hasTail`: a line whose first word names a
Command or a Service is split into the path and its values; a Field still takes
none, and input to an endpoint that takes none is refused in words.
`DynamicBatch::schema()` is new.

`checkBatchPath` has three new statuses: `needs` (values missing; `next` names
the one Enter writes as `name=`), `error` (a value that is not one, a misspelt
name, an argument given twice) and `unnamed` (values typed without names;
`named` is the line with them written in). `input` now means only a struct or
an array argument, which Details fills. `adds` carries `arguments`, and its
message lists the values as they will be sent. `addBatchPaths` adds the typed
request, and names the item with its values in the line's own form, so a copied
item pastes back. `batchPathCandidates` shows a Command's or Service's named
signature - `(reference: f32)` - and, after the path and a space, the arguments
not given yet (`kindCode` `argument`), then a bool's or an enum's values.

A name half typed (`control.Scale fa`) is taken as a name, and a bool's or an
enum's value half typed as that value, not as a wrong value; `name=` waits for
its value quietly. On `needs` the check carries `line`, the line Enter writes:
the next `name=`, or the half-typed name or value completed. The Batch's own
refusals (changes off, a full Batch, items of an earlier connection) now come
as soon as the line names one Command or Service, not once every value is
valid. A Command or a Service that takes input is suggested with `follow` " "
and `continues`: choosing it, or Tab on its whole path, writes a space and
offers its arguments. An argument is suggested as `name=` and a value by its
name, with `completion` the whole line.

`PathAddRow` gains `setText()`, and Ctrl+Enter calls the adapter's `select(text)`
when it has one; `BatchAddRow` uses it to select the endpoint for Details, and
`selectBatchPath` puts each valid value typed into the draft (with changes
allowed; otherwise it only selects), by the argument's place and as the editor
takes it (`Item::editorText`: an enum by its code). On `unnamed` Enter writes
the names in and adds nothing. `selectBatchPath` now refuses only when the
selection holds an edit to lose (BA-1 refused whenever a Command or a Service
was selected).

## The live Plot, first slice (LP-1, November 2026)

`DynamicConfiguratorProvider` gains `watchTraces`, a `WatchTraces`: every
Watchlist value's readings kept in memory by its path - the last ten minutes,
at most 65,536 readings a value - whether it is plotted or not, filled from the
same completed reads `watchCompleted` announces, before the signal. A value
removed from the Watchlist keeps its readings and its line breaks there; a read
that brought no number (a failure, a drained request, a value no double holds
exactly) keeps its place with none. `WatchTraces` also holds which values the
Plot shows (`plotted`, `plot(path)`, `unplot(path)`, at most 16) and their
colours (`colorIndex`), so the Watchlist's menu and the Plot agree.
`plotValue(WatchValueBits)` is the one rule for a reading as a number.

`TracePlot` (QML) shows them: Follow, a chip per value with its newest reading
and ×, and `TraceView`, a C++ `QQuickPaintedItem` that draws the grid, the time
of day under it, one value axis and a line per value - raw while two readings
or fewer fall on a device pixel, else each pixel's first, lowest, highest and
last. It paints with QPainter, so it draws on every backend, the software one
included; the library takes no QtGraphs dependency. A host that links the
module statically calls `registerQmlTypes()` so `TraceView` is a QML type.

Following, the Plot's right edge is the newest reading; new readings only
mark the picture, drawn again at most every 50 ms. All rings together keep at
most 4,194,304 readings (64 MB): past it the oldest go, of values no longer
watched, then not plotted, the plotted last. `stopWatch()` and a disconnect
break every line (`WatchTraces::breakAll`). A text value is refused in words.

The Watchlist row's menu gains a checkable **Plot**. `TrendPanel` and
`TrendHistory` stay for now; the plan retires them when Trend draws with the
same component.

## Moving through the live Plot (LP-1b, November 2026)

`TracePlot` gains the presets `10 s | 1 min | 10 min | All` (All spans every
reading kept and grows while following), Zoom out and Zoom in, and a chip over
the plot's corner while it is held: "17 s behind · Follow (End)". `TraceView`
takes a drag (pan), the wheel (zoom about the pointer), Left/Right (pan a
tenth), +/- (zoom), Home (the oldest reading kept), End (follow), Space/H (hold
or follow) and 1-4 (the presets); panning, zooming and Home stop following,
and the view stays between the oldest and the newest reading kept. New on
`TraceView`: `showAll`, `preset` (10, 60, 600, 0 for All, -1 after a zoom),
`behindSeconds`, `choosePreset()`, `zoom(factor, at)`, `pan(part)`,
`toOldest()`, `follow()`.

## The live Plot's cursor and reference (LP-1c, November 2026)

`TraceView` draws a cursor under the pointer, snapped to the nearest reading
within 8 px, with a dot on each value's reading at or before it; Shift+Left and
Shift+Right step it reading by reading (from the newest in view when there is
none). A click, or Enter, pins a reference whose readings are copied then; a
click on it, or Esc, takes it away. `TracePlot` shows the cursor's time under
the axis ("12:04:31.250", "· 12.4 s ago" while following, "Δ 1.250 s · …"
with a reference) in a row of its own (`TraceView.bottomInset`), and each chip
shows its value at the cursor and its difference from the reference. New on
`TraceView`: `cursorShown`, `cursorUtcMs`, `cursorAgoSeconds`, `cursorX`,
`referenceShown`, `referenceUtcMs`, `referenceX`, `cursorColor`,
`referenceColor`, `dotRingColor`, `bottomInset`, `setCursorAt()`,
`hideCursor()`, `stepCursor()`, `pinReference()`, `clearReference()`,
`valueAt(path)`, `referenceValue(path)`.

## A pictogram says why it cannot act (FL-1a, November 2026)

`IconToolButton` gains `disabledReason`, a few words on why the action cannot
run now ("not connected", "select a file first"). While the button is disabled
the reason follows its name in brackets in the tooltip, in place of the
shortcut ("Show the project folder (no project open)"), and is its
`Accessible.description`; enabled, nothing changes. A disabled control still
gets hover in Qt 6, so the tooltip shows on a disabled button; it is no longer
drawn with the hover fill, which looked as if it could be pressed. Callers that
set no reason see no difference.

## A descriptor's problem in words; values.bin not blocked by it (FL-1b, November 2026)

`DescriptorContent::check(bytes)` returns why bytes are not a descriptor this
program reads, and `DescriptorContent::phrase()` says it in the files-only
card's words: "it is empty", "it is N bytes, more than the 4194304 bytes this
program accepts", "it is version N; this program reads version 3", "it ends
early, at byte N", "its fingerprint does not match its content", or "its bytes
are not laid out as a descriptor (at byte N)" - the byte only where the header
shows it. `DescriptorContent::error()` is that phrase; "not a descriptor this
program reads (status 1, error 3)" is gone.

`OpenFiles::setCodecProblem(codec, sentence)` lets a codec say its failure in a
sentence of its own, `%1` the reason and `%2` the view shown instead (Table,
Text, Hex); `installTelemetryCodecs` gives the Descriptor codec "This descriptor
cannot be read: %1. It is shown as %2.", and a fingerprint that does not match
reads "…, so it was changed after it was built". Other codecs keep "Not a valid
<name> file: <reason>". `FileContent::setCodecs` takes the sentences as an
optional third argument.

The Values codec, and the context's search of the devices' folders, no longer
take a descriptor whose header names the right fingerprint but whose bytes do
not read. values.bin beside such a copy is read with the context's descriptor
of its fingerprint (the connected device's first); with none it waits as before
and `ValuesContent::besideProblem` says "values.bin cannot be decoded:
telemetry/descriptor.bin beside it cannot be read.", which `ValuesView` shows
in place of "No descriptor with this fingerprint was found".

## Files a host keeps read-only; a broken copy's way back (FL-1c, November 2026)

`OpenFiles::setReadOnlyLookup(lookup)` takes, for a device's path, why its copy
is not edited in the Viewer (a sentence), or empty. `FileContent` gains
`readOnlyReason` from it - never for a file opened from disk - and
`setEditing(true)` does nothing while it is set; a path that becomes read-only
ends editing, keeping any edits. Restore the device's version, Save, Undo and
a saved copy elsewhere still work: they are how a broken copy is mended. The
library sets no lookup itself: the host says which files and in which words.

`FileEditTools`' Edit is off with the reason as its `disabledReason`;
`HexPane`'s Edit bytes, Paste over the bytes and Replace… say "(read-only)",
Ctrl+H opens Find with the reason, and the state line says "Read-only" with the
reason on hover. When a codec cannot read the file, `FileViewer` follows the
reason with two links - Restore the device's version (when the device's bytes
are known and differ) and Read again from the device (when `reloadable`) -
in `fileViewerCodecProblemBar`; the reason keeps its `fileViewerCodecProblem`
label. New on `FileViewer`: `codecFailed`.
