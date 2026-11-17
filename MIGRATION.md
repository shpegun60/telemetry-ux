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

## Lanes for booleans and enums on the live Plot (LP-1d, November 2026)

A plotted bool or enum is no longer drawn on the numbers' axis: `TraceView`
gives each a lane of its own under the numbers, a row high (`laneHeight`, at
most 40 % of the graph together): a bool a band while true and a line while
false, an enum a band per state (`laneFillColor`) with its name where it fits
(`laneTextColor`) and a mark where it changes. The value axis spans the numbers
only. `WatchTraces` gains `stateText(path, value)` ("true", "Idle", "Code 7")
and `setStates(path, names)`; the provider fills the names from each enum row's
editor choices. The chips say a state in words, with no difference.

## Zooming the values on the live Plot (LP-1e, November 2026)

The values axis is no longer fixed to the curves in view. `TraceView` has two
modes for it, reported by `valueAuto`: Auto fits every number in view (only the
focused curve while one is focused), and Manual starts as soon as the values are
zoomed or panned by hand - Ctrl and the wheel, or the wheel over the value
labels (`valuesArea`), a drag over the labels, Up/Down and Ctrl+Up/Down - and
keeps its range through Follow, the presets and time zoom. A, a double-click on
the labels or `autoValues()` returns to Auto. `focusedPath` (read-write) fits
the values to one curve and draws it heavier; setting it returns to Auto. New
invokables: `zoomValues(factor, at)` and `panValues(part)`. `TracePlot` shows
an "Auto (A)" chip in Manual, a "Fitted to ..." chip while focused, and focuses
a curve on a click on its chip's name.

## Curve settings and measurement points on the live Plot (LP-1f, November 2026)

Each curve has a look of its own again, kept for its path whether plotted or
not: `WatchTraces.setAppearance(path, color, k, b)` sets its colour (`""` for
its palette place, else `#rrggbb`) and the multiplier and offset it is drawn
with, y = k x reading + b, for display only - the rings, the chips and the
cursor keep the readings. `setPoints(path, mode)` (0 as the Plot, 1 show, 2
hide) and the Plot-wide `showPoints` draw a dot at each reading of a raw curve.
`appearanceChanged` and `appearanceRevision` report a change. `capture()` and
`restore(map)` carry the plotted curves in order, every look and `showPoints`,
for a project to keep. `TraceView` gains `curveColor(path)` and `drawnDots`.
`TracePlot` opens a curve's settings from its chip's colour mark, tags a scaled
chip ("×100 +0.5"), and has a chip menu and a graph menu (right-click, Menu,
Shift+F10).

## Batch polish (BA-3, November 2026)

`BatchPanel` makes the item just added the current one, highlighted and in
view, while the keys stay in the "+" row; a click makes an item current. An
item's name cut short shows whole on hover, and each item row has the
accessible name "Item 4, service control.Scale value=2 factor=3, Ready".
Beside the add row's notice, "Fill in Details" (tooltip "Fill in Details
(Ctrl+Enter)") does by pointer what Ctrl+Enter does: it selects the Command or
Service the line names in the Device tree with the values typed there; it
takes no focus and shows only while the line names one.

## The Watchlist holds 8192 values, the Plot's rings 256 MB (WL-7, November 2026)

`watchlistRowLimit` is 8192 (it was 1024) in both providers, for whole Fields
and values inside struct and array Fields; `watchLimit` reports it, and
adding past it still counts the rest as `capacity`. A cycle then needs at
most 256 ReadMany portions of 32. `WatchTraces::keptTotal` is 16,777,216
readings, 256 MB (it was 64 MB); the drop order and the ten minutes and
65,536 readings a value are unchanged. Code that assumed 1024 should read
the constant.

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

## What a project's copy is before a read replaces it (FL-1d, November 2026)

`DeviceFileTransfers::copyState(localFile, devicePath)` (static, invokable)
returns `exists`, `known` (the device's bytes as read are kept and check),
`differs` (bytes of the copy that are not those, a difference in length
included), `savedAt`, `readAt` and `written`. A host asks before reading the
device's file over a copy that exists and differs, or whose device's bytes are
not known; opening reads over a copy the same as the device's bytes as
before. Nothing else changes: `openIn` still replaces the copy it is given.

## Plotting from the Watchlist, and the graph's menu (LP-1g, November 2026)

`WatchRows.plotValues(row)` gives the values the Plot takes for a row - every
value under a node, or the selection's when the row is in it - as
`{path, number}`, `number` false for a value of no number type read as text.
`WatchlistPanel` uses it for Ctrl+P and its row menu ("Plot 3 values" on a
selection or a node; every value on the Plot already takes them all off) and
refuses text in words. `TracePlot`'s graph menu gains Follow, Pin reference
here, Remove reference and Remove all curves; Delete on a chip's settings
button takes that curve off.

## Gaps and values no longer watched on the live Plot (LP-1g, November 2026)

`WatchTraces.breakAll(reason)` now records why nothing was read
(`PauseReason::Stopped` from stopWatch, `Disconnected` when the provider
retires) as a `Pause` from the break to the next reading; `pauses()` keeps
the last 1,024. `TraceView` fills each with `gapColor` (TracePlot passes
`Theme.surfaceAlternate`) and captions it "Watchlist stopped 12:05:10" or
"Disconnected 12:05:10" where the words fit; `drawnGaps` counts them.
`WatchTraces.watched(path)` and `lastReadUtcMs(path)` tell a value taken out
of the Watchlist: its chip says "not read since 12:05:10" in the muted colour,
and its line ends in an open circle (`drawnEnds`).

## The session runs only when it has work (SDK-2b, November 2026)

The SDK's Qt `ConnectionSession` no longer runs a pass every 5 ms
(telemetry-sdk #63). It runs one when it has work, publishes `progressed()`
only after a pass that did something, and keeps a 1 s heartbeat while idle.
UX is pinned to that SDK and follows it:

- **Requests put straight into the session's Workbench wake it.**
  `DynamicConfiguratorBinding` has a new signal, `submitted()`: once for every
  request a provider puts in the owner's queue - Refresh, Apply, Command,
  Service and both steps of a Watchlist row write - synchronously, once the
  queue has accepted it, never for a refusal. `QtConnectionBinding` connects it
  to `ConnectionSession::wake()`. A host with a binding of its own over a
  session's `workbench()` or `storage()` must connect it too
  ([DynamicConfigurator.md](doc/DynamicConfigurator.md#ownership-and-progress));
  otherwise each such request waits for the heartbeat, up to a second. A host
  that progresses its owner on its own clock may ignore the signal.
- **Previews keep a timer of their own.** `EndpointPreviews` started its next
  cycle, and moved the ages shown on, only when the session's progress polled
  it; with an idle session they stopped. A single-shot timer now wakes
  `poll()` for the next cycle and for the next whole second, only while Fields
  are in view and the source is current. Replies are still taken in on
  `progressed()`. New: `nextPollInMs()`, -1 while the timer is off.
- **Transfers stay live.** `DeviceFileTransfers` republishes the running
  transfer's row on a 250 ms timer, which stops once nothing runs, so the rate
  and "waiting for the device…" stay current while the session waits on the
  device and publishes nothing.
- **The Inspector example's TCP mode** wakes its session after each accepted
  Refresh, Apply, Command, Service and Watchlist read, and on its Configurator
  panels' `submitted()`.
- The Watchlist has its own timer and is unchanged. Code of your own that
  refreshes on `progressed()` as if it were a clock needs a timer of its own.

Measured over localhost TCP, Windows 11, MinGW 15.1, Qt 6.12.0, each request
sent with the session at rest (`tests/qt_connection`): with the new SDK and the
UX before this change, Refresh, Apply, Command, Service and a row write took
930-950 ms each (the first Refresh 350-370 ms, by the heartbeat's phase); with
the wake, 0-1 ms each, in three runs of each. The Inspector's TCP Refresh,
Apply and Configurator panel Refresh went from 930-950 ms to 0 ms
(`tests/inspector_tcp`). The `tests/qt_connection` native check, whose waits
had gone to the heartbeat, ran in 75 s before and runs in 8 s now. Before,
previews over TCP were never read at all; now a preview at a 1 s interval is
read again 1000-1047 ms after it landed, the session publishing nothing in
between. A download the device holds back was published 0 times in 6 s before;
now it is published 5-6 times in about 3 s, the rate and then "waiting for the
device…" shown on time, while the session publishes nothing.



## Details: the cell is the editor (BA-4·0a, November 2026)

- **No Edit button.** `EndpointPanel` no longer shows Edit. A Field's values are
  editable in place while it has been read, is writable and changes are
  allowed; the first valid change opens the draft and a draft that is equal to
  the value read again, with no invalid text left, closes itself. Cancel and
  the accent Apply show only while a draft exists, and Read hides meanwhile.
  Execute and Call are the accent action of a Command and a Service. A check
  that clicked `editButton` opens a cell instead: click its `valueText` (it
  carries `scalarName`), or call the cell's `begin()`.
- **`setScalarText` opens and closes the draft.** The Dynamic and Static
  providers and the Inspector example accept text for a Field that has no
  draft yet when it may be edited (`canEdit`), and open the draft then.
  `beginEdit()` is unchanged for hosts that open a draft themselves. A host's
  own provider for `EndpointPanel` either does the same or opens a draft in
  `beginEdit()` before the panel's first `setScalarText`; the panel calls only
  `setScalarText`.
- **`ValueModel::setEditOffer(bool)`** makes a Field's scalar rows `editable`
  before a draft exists. The Dynamic provider and the Inspector example keep it
  up to date; a host with its own `ValueModel` owner calls it when its Field may
  be edited. New role `takes` (Dynamic and Static): what a cell says while its
  text is no value, "Enter a number from 0 to 255.". The Static `ValueTree`'s
  `editable` role is true for a read, writable Field while changes are allowed.
- **`ValueCell.qml`**, new in the module: the value cell Details uses for its
  values and its response, and the Batch will use. It is a value until opened
  (a click, or F2 or Enter on its row), then a text field or a choice one row
  tall; edits are live; Enter keeps, Tab and Shift+Tab keep and move on, Escape
  puts back the value it had when opened, and leaving with text that is no
  value puts back the last value. Ctrl+Enter applies, executes or calls. The
  editor is a `QtQuick.Controls.Basic` TextField in every host style.
- **Changed behaviour for checks:** blurring a cell with invalid text no longer
  keeps the raw text; the last value comes back. "Enter a valid value." is gone:
  the words name the type's range. Bool choices read "false" and "true" (their
  codes stay in `editorChoices`). Integers take hex as the value's bits at its
  width (`detail::parseScalar`), as the Watchlist does.
- **Details reads a picked Field once while changes are allowed**, so its
  cells can be edited at once: one FieldRead per selection that has no value
  yet, none while another request is pending, none with changes off. A check
  that counts reads after selecting a Field with changes allowed counts one
  more; one that wants the unread definition turns changes off first.

## Details looks like the family (BA-4·0b, November 2026)

- **Header, line 2, status line.** The header holds the path (elided in the
  middle) and the panel's actions in a row at the right, which take their own
  row below 360 px. Under it a `ProgressLine` (`endpointProgress`) runs while
  the provider is busy; the BusyIndicator is gone. Line 2
  (`endpointSelectionInfo`, a caption) says the selection in words, from the
  provider's `selectionInfo.detail` ("Field · f32 · 4 bytes · read only"),
  which the Dynamic provider now supplies; other providers get it composed.
  The status line (`endpointStatusLine`) replaces "Local draft",
  `definitionUnreadNote`, "Previous connection" and the bottom
  `writeOutcomeLabel`, `commandOutcomeLabel` and `serviceOutcomeLabel`, which
  are removed. After a disconnect a category panel's status line keeps its
  outcomes as "Before the connection dropped: …". A check that read those
  labels reads the status line.
- **`DynamicConfiguratorProvider::detailsStatus`** (new, `{text, failed,
  tip}`): "Read 12:04:31", "Edited, not applied · 2 changes", "Applied
  12:05:02 · read back", "Executed 12:05:10 · accepted", "Called 12:05:21 ·
  took 3 ms", "Reading…", "Not read yet", "Not executed yet", "Not called
  yet", and on a failure "Couldn't write: the device does not take this
  value." with `failed` true (an error glyph, the text in the primary colour).
  A provider without it gets a status line made from its outcomes.
- **Tables.** No root rows: a struct's or a request's members are the top rows
  (`ValueModel::contentRoot()`, used as the TreeView's `rootIndex`; the model
  keeps its root, so C++ code that walks `index(0, 0)` is unchanged). Rows are
  striped with `surfaceAlternate`, hovered with `stateHover`, selected with
  `stateSelected` (`stateSelectedInactive` without focus). Columns follow
  family spec A3: Name fills, Value grows to the widest value shown, Type to
  the widest type (40 to 128 px) and hides before Name is narrower than 96 px;
  nothing scrolls sideways any more (the horizontal scroll bars are gone).
  New token `Type.columnGap` (12 px at the default body).
- **Section headings and captions.** A Command's values are headed
  "Arguments", a Service's "Request" and "Response", each once, under the
  caption "Start at zero, not at the device's defaults."; a Service that takes
  nothing says "Takes no request values.". The panel has no card and no radius
  any more (`framed` keeps only its padding).
- **Unsent and stale.** New `ValueModel` roles `unsent`, `stale` and
  `staleNote`, properties `stale` and `staleNote`, and `texts()`,
  `setBaseline()`, `clearBaseline()`, `unsentCount()`, `baselineLine()` and
  `setStale()`. A draft's value that differs from the value read, and a
  request's value that differs from the one last sent, have a 2 px accent line
  at the row's left. A Service's response whose request changed since the call
  is italic and muted under "From the call at 14:02:11, with value=2 factor=3;
  the values changed since." (`responseStaleNote`).
- **For the Batch's pane (BA-4a)** the panel takes `title`, `infoPrefix`,
  `actionsComponent` (replaces Read, Cancel, Apply, Execute and Call) and
  `emptyResponseText`; `refusalText` and `contextHelp` may be set.
## One history for Undo and Redo: the core, the Watchlist and files (UR-1, UX-A, November 2026)

The Watchlist's single Undo slot and the hex editor's own undo stack give way
to one kind of history, `UndoHistory` (README, "Undo and Redo"). A host makes
one for its workspace and gives it to its providers; a file keeps its own.

- **`undoWatch()` and `canUndoWatch` are removed: use `provider.history`.**
  Both providers have `history` (an `UndoHistory*`, read and write, null by
  default, `historyChanged`). A host that wants Undo makes `UndoHistory {}`
  (call `registerQmlTypes()` first, as for `HexView`) and assigns it; its
  `undo()`, `redo()`, `canUndo`, `undoText` and `undoSerial` replace the old
  pair. Without a history nothing is recorded. Steps stack: another change no
  longer takes the last one's Undo away, it goes on top of it.
- **`FileContent.canUndo`, `canRedo`, `undo()` and `redo()` are removed: use
  `content.history`.** Its steps are named with the file ("Overwrite 4 bytes in
  values.bin"); it keeps 1000. Typing merges into the step before only within
  two seconds now, as well as where it stopped and not across a save.
- **More is a step, and by path.** `addWatch`, `addWatchMany`, `removeWatch`,
  `removeWatchRows` and `clearWatch` record a step as the boxes and
  `addWatchPaths` did; so do `chooseWatchFormat`, `setWatchRowFormat` and
  `resetWatchFormats`. A step holds rows by path, not by key, so it can be
  undone with no device - the rows come back kept by their paths and are found
  again on the next device that has them - and on a device with another
  descriptor, where what it lacks comes back as rows that say so. Format
  choices within two seconds merge into one step. Restores stay out of the
  history: `restoreWatchBindings` no longer drops anything, it adds nothing.
- **Merging by key.** `UndoHistory::record(text, parts, mergeKey,
  mergeWindowMs)`: steps of one key merge within two seconds unless the caller
  says otherwise; `0` merges for as long as the key stays the same, and the
  caller ends it by changing the key (an editor, when its cell is left).
- **Results carry the step.** `setWatchMany`, `addWatchMany`, `removeWatchRows`,
  `addWatchPaths` and `resetWatchFormats` add `serial` to their map: the step's,
  0 when none was recorded. A notice that offers Undo shows it while
  `history.undoSerial` is that serial.
- **`StaticConfiguratorProvider.removeWatchRows(rows)`** is new, as
  Dynamic's, so the Watchlist removes several rows as one step there too.
  Static's steps wait while it polls; its `blockedReason` says so.
- **`DynamicWatchlist.restore(removed)` is replaced by `putBack(rows)`**, rows
  by place and path, which needs no device.
- The panels' own Ctrl+Z, notices and the two questions are UX-B's (below).

## The live Plot's changes as steps (UR-1, UX-D, November 2026)

The live Plot records into the same history as the Watchlist (README, "Undo
and Redo").

- **`WatchTraces.history`** (read and write, `historyChanged`) is new. A
  Dynamic provider gives its traces its own history when it is given one; a
  host with traces of its own sets it there. Null records nothing.
- **Steps.** `plot(path)` ("Plot device.Voltage"), `unplot(path)` ("Remove
  device.Voltage from the Plot"), `setAppearance` ("Change the curve of ..."),
  `setPoints` ("Change the points of ...", one curve's changes within two
  seconds merging) and `showPoints` ("Show measurement points", "Hide
  measurement points") are each a step. Kinds: `plot.curves` (by path, with
  the curve's place and colour), `plot.look` (by path: colour, k, b, points)
  and `plot.points`. Undo puts a curve back in its place with its colour; a
  colour another curve took meanwhile gives way to the first free one.
- **New: `plotMany(paths)` and `unplotMany(paths)`**, several curves as one
  step: the Watchlist's Plot (Ctrl+P on a selection or a node) and the Plot's
  "Remove all curves" ("Remove all curves from the Plot") use them, so one
  press is one step. `plotMany` answers `{added, refusal, last}`.
- **`restore(state)` records nothing**: a project's Plot put back is no step.

## The Batch's changes as steps (UR-1, UX-C, November 2026)

The Batch records into the same history as the Watchlist and the Plot (README,
"Undo and Redo").

- **`DynamicBatch::setHistory(history)`**; a Dynamic provider gives its Batch
  its own history when it is given one. Null records nothing.
- **Steps, kind `batch.items`.** Add ("Add device.Voltage to the Batch", "Add 3
  items to the Batch"), remove ("Remove device.Voltage from the Batch"), move
  ("Move device.Voltage to item 2 in the Batch"; moves of one item within two
  seconds merge, and one back where it started leaves none) and Clear ("Clear
  the Batch"). Execute Batch is never a step; a step waits while a Batch is in
  flight: "Wait for the Batch to finish."
- **`BatchRow.uid`** is new: a row's identity to the history, given when the
  Batch takes the row and kept through Execute. `BatchRow` gains `operator==`
  and is a Qt metatype.
- **Rows go back whatever the connection.** Undo puts rows back by uid in
  their places, skipping the session refusal: a row of an earlier connection,
  or put back with none, comes back historical, as a reconnect leaves rows -
  shown, never sent again until cleared.
- **The serial reaches the panels.** `addBatchPaths` answers `serial`, and
  `DynamicBatch::lastSerial()` gives the last change's. `PathAddRow`'s adapter
  answers `add(lines) -> {notice, undoable, serial, clear}` and the row emits
  `added(notice, undoable, serial)`; `BatchAddRow` and `WatchAddRow` give the
  step's serial, and the Batch's additions are undoable.

## Ctrl+Z in a window: the router, scopes, notices and buttons (UR-1, UX-B, November 2026)

The panels stop answering Ctrl+Z themselves; a window has one router (README,
"Ctrl+Z in a window").

1. **Panels no longer take Ctrl+Z: one `UndoRouter` per window**, its
   `workspace` the host's `UndoHistory`, and `UndoScope.refusal` on areas that
   act on the device - Details: `UndoScope.refusal: details.undoRefusal`, the
   words `EndpointPanel.undoRefusal` holds. The Watchlist's
   `Keys.onShortcutOverride`/`onPressed` for Ctrl+Z, its `undo()` and its
   `recorded` property, and `HexView`'s Ctrl+Z and Ctrl+Y are gone. A
   `FileViewer` is an `UndoScope` for its file's history, so Ctrl+Z in it takes
   the file's step. `registerQmlTypes()` is needed for the router, as for
   `HexView`.
2. **`FileEditTools` has no Undo and Redo buttons: use `UndoButtons {
   router }`.** Its Write to the device says "Undo changes only the copy on
   this computer." in its tooltip; `IconToolButton.note` is new for such a
   second clause.
3. **The questions are gone**: `clearWatchDialog` and `removeWatchDialog`, with
   `clearWatchConfirm`, `clearWatchCancel`, `removeWatchConfirm` and
   `removeWatchCancel`. Clear the Watchlist (no ellipsis) and removing several
   values act at once and say so with Undo ("Cleared 66 values · Undo");
   `WatchlistPanel.removeAt(row, own)` loses its `confirmed` argument;
   `clearAll()` is new.
4. **One notice component, `UndoNotice`.** The Watchlist's notice line is one
   (`watchNotice`, its link `undoNoticeLink`, the old `watchNoticeLine` and
   `watchUndo` gone); so is the Device tree's status line
   (`endpointWatchSelectionStatus`, its link `undoNoticeLink`;
   `endpointWatchUndo`, `undoStatusStep()`, `canUndoStatus`, `statusSerial`,
   `statusRevision` and the writable `watchStatusFull` gone; `watchMarkStatus`
   is read-only). After an undo a notice says "Undone: ... · Redo".
5. **`EndpointTreePanel.watchBoxes`** is new (default true): false offers no
   watching at all, for a second device's tree.
6. **`UndoHistory`**: `done` gains `kinds`; `redoBlockedReason`,
   `markNoticed()` and `noticed()` are new; a step taken ends the irreversible
   note. Both providers call `noteIrreversible` as Apply, Execute, Call and a
   Watchlist row write are sent, and Dynamic's Execute Batch; Static's
   `apply()`, `executeCommand()` and `callService()` too. `FileContent.writing`
   is new: while the host writes the copy to the device, the file's steps wait.

## The live Plot's time axis from the start of reading (LP-1h, November 2026)

`TraceView.fromStart` switches the time axis between the time of day (the
default) and the time since reading last started: the end of the last stop
or disconnect up to the right edge, else the first reading, whose time of
day is `originUtcMs`. Labels read "12 s", "1:30", "1:02:00" or "−0:12".
`TracePlot`'s axis caption is now a flat button, "Time of day ▾" or "From
start ▾", with a two-item menu; with the axis from the start, the cursor chip
reads "17.911 s · 22:27:02.379".

## A recorded run on the Plot's view (TR-2, November 2026)

`WatchTraces::setRecorded(path, readings, states)` and `setAnchor(anchor)`
let a host show a recording with the same TracePlot: a path's readings, one
run's, in time order and NaN for a gap, with the anchor that gives them a
time of day. The live rules do not apply to them (no ten minutes, no cap per
path) and the path counts as watched. `TraceView` fits its values afresh when
its `traces` change or a curve is plotted or removed.

## Dragging rows onto the Plot and the Watchlist (LP-1g3, November 2026)

The Watchlist's list and the Device tree no longer scroll by a mouse drag
(`acceptedButtons: Qt.NoButton`; the wheel, a touchpad, the scroll bar and a
touch screen still scroll): a mouse drag takes rows instead, as an automatic
drag with the type `application/x-telemetry-paths`, JSON
`{"from": "watchlist" | "device", "paths": [...]}`, and the paths as plain
text. A Watchlist row carries its values (`WatchlistPanel.dragPaths(row)`, the
selection's when the row is in it); a Device tree row its endpoint's path, a
group or a category every endpoint under it
(`EndpointTreePanel.dragPaths(index)`). `TracePlot` gains an optional
`provider`, `dropPaths(payload)` and a `notice` line: a Watchlist drag plots
its values; a Device tree drag joins the Watchlist first and plots the values
under what was dropped. `WatchlistPanel.dropPaths(payload)` adds a Device tree
drag. Both show a frame and what a drop will do while a drag is over them.

## A recorded run on the Plot's view (TR-2, November 2026)

`WatchTraces::setRecorded(path, readings, states)` and `setAnchor(anchor)`
let a host show a recording with the same TracePlot: a path's readings, one
run's, in time order and NaN for a gap, with the anchor that gives them a
time of day. The live rules do not apply to them (no ten minutes, no cap per
path) and the path counts as watched. `TraceView` fits its values afresh when
its `traces` change or a curve is plotted or removed.

## Analysis on the Plot: box zoom, Back, copy and save, Measure, find (November 2026)

`TraceView` gains, from the designer's analysis audit: a box drawn with Ctrl
and a drag (`zoomToBox`, `boxShown`) and a way back through every view moved
by hand (`goBack`, `canGoBack`; Backspace, Alt+Left); the readings of the
range as text and CSV (`valuesText`, `readingsInRange`, `saveValues`); an end
pin beside the reference (`endShown`, `endUtcMs`, `endX`, `pinEnd`,
`clearEnd`; Shift and a click, Shift+Enter) making the range measured,
copied and saved (`measureRange`); per-curve statistics over it
(`statistics(path)`); `putCursorAt(utcMs)`; and `stepChange(direction)` with
`findFailed(words)` (Ctrl+Right, Ctrl+Left). `TracePlot` adds the Back
button, the Measure table (M), copy picture (Ctrl+C), copy values
(Ctrl+Shift+C), save as CSV, the graph and chip menu entries, and says each
result in its notice line. `Clipboard.setImage` and `hasImage` are new;
`WatchTraces.categorical` is invokable from QML.

## The Batch's items carry their values (BA-4a, November 2026)

- **The right pane is Details.** `BatchPanel` shows the selected item in an
  `EndpointPanel` over the new `provider.batchItem` (a `BatchItemDetails`):
  a Command's arguments, a Service's request and response, a write's value, a
  read's value read, under the item's path and "Item 3 · Service · …". Choosing
  an item shows it at once: the Select button is gone, and the keys move the
  selection too.
- **Values are set on the item, live.** A cell's text goes into the item's
  request as it is typed (`BatchItemDetails::setScalarText`), the row's line
  follows ("value=2 factor=0.1"), and each value is one step of the central
  Undo ("Set factor of item 3", kind `batch.request`, merged until the cell is
  left: `closeValue()`). Items of an earlier connection keep their values and
  are not edited; their steps are skipped.
- **Read | Write.** A Field that can be read and written has a Read | Write
  switch in the pane's header; a write starts from the value last read, else
  from the type's initial value; the change is one `batch.items` step that
  keeps the item's uid ("Make item 2 a write").
- **`BatchRow`** gains `sent` (the request as Execute last sent it), `line` and
  `at`; `BatchListModel` the roles `line`, `changed` (the request differs from
  what was sent), `at` and `uid`, and `update(row, value)`.
- **`DynamicBatch`** gains `values()`, `selectedRow()`, `canEdit()`,
  `setScalarText()`, `closeValue()`, `setRequest()`, `replace()` and
  `lineOf()`. A read's value is now in `values()`, not `responses()`, which
  holds a Service's response only.
- **`ValueModel::resetDraft(RequestEditor)`**: an editable projection of a
  request editor's draft.
- **Removed:** `DynamicConfiguratorProvider::batchResponses` and
  `batchResponseEndpoint`, and the Batch panel's response tree
  (`batchResponseTree`, `batchResponseHeading`, `batchResponseEmpty`). Read
  the selected item's values and response from `batchItem`.
- `EndpointPanel` takes `emptyText`, what it says with nothing selected, and
  asks a provider with `closeValue()` to close a value when its cell closes.

## The Batch's "+" is a finder (BA-4b, November 2026)

- **Commands and Services are added at their initial values.** A Command or a
  Service that takes input is added like one that takes none, its request at
  the SDK's initial values (zero, false, an enum's first named enumerator:
  `RequestEditor::initialize`, from telemetry-sdk #64), and its values are set
  on the item. The line under the row says what it starts at: "Adds a call of
  Service control.Scale as item 3, with value 0 and factor 0; …". A write-only
  Field is added as a write.
- **Values typed after a path are refused:** `Set values on the item, not here:
  remove "value=2".` The `needs`, `unnamed` and `input` statuses of
  `checkBatchPath`, its `arguments`, `next`, `line` and `named` keys, and the
  argument and value candidates are gone.
- **Headings.** `batchPathCandidates` lists its candidates under headings,
  Fields, Commands and Services in that order: a heading is `{heading: true,
  path, completion, word, category, count, shown}`, a candidate carries
  `section`. Empty text lists up to eight of each kind. No group rows. A kind
  word first may be `field`, `command` or `service`, singular or plural, or
  `read` (`endpoint_paths::kindCategory`). `PathAddRow` skips headings with the
  keys, and a heading's click writes its kind word before the text.
- **Removed:** `DynamicConfiguratorProvider::canAddBatch(operation)`,
  `addBatch(operation)` and `selectBatchPath(text)`; the Batch panel's
  operation box, **Add selected draft** and **Fill in Details**;
  `BatchAddRow`'s `selected` signal and its Ctrl+Enter; `PathAddRow`'s
  `follow` and `continues`; and from the installed `RequestArguments.hpp`,
  `parse`, `check`, `Given`, `Item` and `Result` (`arguments`,
  `signatureText` and `valueType` stay). A host that added a prepared draft
  with `addBatch` adds the path with `addBatchPaths` and sets the values on
  the item (`batchItem.setScalarText`).

## The Batch's rows, drop and status line (BA-4c, November 2026)

- **Rows.** The Up, Down and Remove buttons are gone. The hovered or current
  row shows `...` (`batchRowMenuButton`, the row's menu) and × (`removeBatchButton`,
  "Remove item 3", Delete). A right click or Shift+F10 opens the menu (`batchRowMenu`):
  Move up (Alt+Up), Move down (Alt+Down), Make it a read, Make it a write (a
  Field that can be both, as the pane's Read | Write switch) and Remove (Delete).
- **Drop.** Paths dragged from the Device tree or the Watchlist
  (`application/x-telemetry-paths`, as the Watchlist and the Plot accept) can be
  dropped on the list (`batchDrop`): a focus-ring frame and "Drop to add 3 items
  to the Batch"; they are added as the "+" row adds them, at their initial
  values (`BatchPanel.dropPaths(paths)` over `addBatchPaths`).
- **The status line in words.** `DynamicBatch::status()` (`batchStatus`) says
  "Sending 3 items in one request…", "Executed at 14:02:11 - 3 of 3 done." (or
  "…; item 3: invalidargument." for the first item not done), and for a run that
  failed as a whole: "No reply in time. The items may have run; nothing is sent
  again by itself.", "The reply was lost: the items may have run. Check the
  device before executing again.", "The device was busy and ran nothing.
  Execute the Batch again in a moment." Items of an earlier connection: "These
  items are from an earlier connection and are never sent again. Clear them to
  start a new Batch." It is empty while there is nothing to say. The old words
  ("Pending - one ordered Batch request", "Batch reply received - inspect each
  item", "OutcomeUnknown - may have executed; no automatic retry", "QueueFull -
  …") are gone; a host that matched them matches the new ones, or the item
  facts in the `result` role. An item with no checked outcome reads "Outcome
  unknown", "No reply" or "Not sent".
- **`batchAttention`** (`DynamicBatch::attention()`): the status line asks for
  attention - a run that failed, or an item not done; the panel draws it in
  the warning colour, else muted.

## The Batch: one-line rows, drag to reorder, Execute as the accent (BA-4c, November 2026)

- **One line per item.** A Batch row (`batchRow`) is `Type.rowHeight` (22 px)
  tall and striped, as every table of the family: the number (`batchRowNumber`,
  caption monospace, muted, right-aligned), the kind in its own column
  (`batchRowKind`: "read", "write", "command" or "service", caption,
  secondary), the line in monospace (`batchRowName`, whose `text` is the whole
  line: the path bright and cut short in the middle, its values quieter, with
  the whole line in a tooltip when cut) and the outcome (`batchRowOutcome`,
  caption, right-aligned). The 64 px two-line rows and the per-row "Draft" are
  gone; an item being sent reads "Sending…", one of an earlier connection
  "earlier connection", muted. A failure carries the error glyph and the error
  colour; `...` and × keep their place on every row, shown on the hovered or
  current one, so nothing moves on hover. Selection, hover and the focus frame
  are Details'.
- **`failed`**, a new role of the Batch's list (`BatchListModel::FailedRole`):
  the item's outcome is a refusal, an unreadable value, or no checked outcome
  after a run. A host's own list without it shows no failures.
- **Drag to reorder.** A mouse drag on a row moves the item (`batchRowDrag`, a
  `DragHandler`); a 2 px accent line (`batchMoveLine`) shows where it would
  land, and the release makes one move. Touch, the wheel and the scroll bar
  still scroll: the list takes no mouse buttons for flicking
  (`acceptedButtons: Qt.NoButton`, as the Device tree). Paths dropped from
  other panels (`batchDrop`) still join the Batch as before; the reorder drag
  carries no mime data and never reaches that drop.
- **`moveBatchTo(row, to)`** on the Dynamic provider, over the new
  `DynamicBatch::moveTo(row, to)`: the item at `row` to place `to`, the others
  closing up, the selection following its item. It is one UR-1 step of the
  existing `batch.items` kind ("Move device.B to item 3 in the Batch"), merged
  with that item's other moves within two seconds as `move()` is; only the
  moved item's place is stored, and Undo puts every item back. `move(row,
  delta)` is now `moveTo(row, row + delta)`. A host provider without
  `moveBatchTo` is moved one place at a time through `moveBatch`.
  `BatchPanel.moveItem(from, to)` is the panel's entry for both.
- **The header.** The fold (`batchToggle`) keeps the panel's title and counts
  "3 of 32 items"; Clear (`clearBatchButton`) is flat, beside it at the left,
  with the tooltip "Remove every item. Ctrl+Z puts them back."; Execute Batch
  (`executeBatchButton`) is the panel's one accent button at the right, as
  Details' Apply, Execute and Call, with the tooltip "Sends every item in one
  request, in order. This cannot be undone." A `ProgressLine`
  (`batchProgress`) under the header shows a run. The panel's background is
  `Theme.surfaceWindow`, with no card or radius.

## The Batch's "+": Enter adds at once (November 2026)

- **Enter adds.** A half-typed name ("Volt", "Enter takes measure.Voltage.")
  used to only write the first suggestion into the field, and so did Enter on
  a suggestion reached by the arrow keys or after a heading's click: Enter
  seemed to do nothing until it was pressed again. Now Enter, or the keypad's
  Enter, adds at once: the suggestion the keys moved to; else the endpoint the
  whole line names (a whole path is added as typed, even when longer paths are
  listed above it); else the highlighted or first suggestion. A name two
  endpoints share, with the list shut, opens the list first; a heading or a
  container is still narrowed to or stepped into. A line that names nothing
  adds nothing and says why, as before.
- **`PathAddRow.enterAdds`** (default false) turns this on, and
  **`PathAddRow.highlightBest`** (default false) highlights the best match as
  the suggestions come, so the row shows what Enter takes. `BatchAddRow` sets
  both; the Watchlist's row keeps its own behaviour. `PathAddRow.take(index)`
  is what Enter does with a suggestion, and `pickedByKeys` says whether the
  highlight was moved by the arrow keys.

## ValueCell: every choice with its code (November 2026)

- **Codes beside the names.** A bool's list and closed box read "false (0)"
  and "true (1)" (they had read false and true since BA-4); an enum's read
  "Idle (0)", and so does an enum's closed cell, which had shown the name
  alone. This holds wherever ValueCell is used: Details, the Batch's request
  and write cells and any host table.
- **`ValueCell.hexCodes`** (default false) writes the codes in hexadecimal,
  "Idle (0x00)", padded to the type's width, as the Watchlist's hex form
  shows them (WL-8).
- A host check that compared a bool choice's `currentText` with "false" or
  "true" compares it with "false (0)" or "true (1)"; the value written is
  still `false` or `true`.

## The designer's fixes to the Plot's analysis tools (November 2026)

From the design review of the analysis tools. **Behaviour changes:** "From
start" counts from one fixed moment, the anchor (the first reading of this
run, or a recording's start), no longer from the end of the last stop before
the right edge, and the CSV's and Copy values' "Seconds from start" count
from the same moment, so a row matches the axis and the cursor's chip; a time
label the edge would cut is left out (its grid line stays); a dropped
selection passes text values over and plots the rest as one step of the
history (`plotMany`), stopping only at the sixteen-curve limit. `TraceView`
gains `endValue(path)` (the reading at the
end pin), `chipColor` and `chipHeight` (the box's size is drawn on a chip),
`changeFound()` (emitted when `stepChange` finds its change), and, for checks,
`timeLabelRects` and `boxCaptionRect`; `WatchTraces.refusal(path)` is
invokable from QML. `TracePlot`'s Measure table says "At cursor", "At end" or
"Newest" over its first column and gives a state's reading there; it keeps a
Name column of nine body characters and shows seven, five or three figures,
the rest in the row's tooltip; its rows, like the chips, stop at a height and
scroll. Mean and Std dev show one decimal more than Min and Max. The end pin
has its own chip (`plotEndChip`). Copy picture takes the table or chips and
the graph (`plotPicture`) without the cursor. `notice` gains `noticeKind`
and `tell(words, kind)`: "Copied …", "Saved …" and a failed find leave after
5 s, refusals stay until ×. `dropWords(payload)` gives what a drop will do.

## The Plot's header in a narrow dock (November 2026)

`TracePlot`'s header now fits a 320 px dock instead of holding the whole Plot
at its own width. As the Plot narrows it gives way in a fixed order,
measured from the controls as the Watchlist's toolbar is: the zoom buttons
hide (the wheel, + and - still zoom); then 10 s | 1 min | 10 min | All fold
into one button showing the span, "1 min ▾", with the four in its menu
(`plotSpanButton`, `plotSpanMenu`; keys 1 to 4 still choose); then Follow
becomes an icon-only toggle, "Follow the newest reading (End)"
(`plotFollowIcon`, the new `ToolIcon` "follow"); and last the time axis
switch reads "Clock ▾" or "Start ▾", its accessible name unchanged. Back and
Measure always stay. `narrowStep` (0 to 4), `headerMinimum` and `spans` are
new. The graph keeps at least `graphMinimumHeight` (six rows): the chips and
the Measure table scroll before it shrinks below that. The pins' and the
cursor's chips step beside the axis switch, and a chip whose line is on the
plot area's last pixel shows (`onPlot(x)`).

## The Trend drawn by TracePlot (TR-2, November 2026)

`TracePlot.recording` draws a recorded run, the Trend: its words say so (×
"Remove from the Trend; the recording is unchanged", the axis "From run start
(s)"), a chip with no cursor gives the reading at the right edge of the view
(`TraceView.valueAtRight(path)`), nothing is dropped on it, and Follow, its
menu entry and the behind chip appear only while `writing`. `runName` names
the run in the graph's accessible name. `leading` and `trailing` take the
host's controls at either end of the header row. `emptyText`, `emptyHint`,
`waitingText` and `message` let the host word the empty, waiting and
over-the-graph states. In a narrow dock a recording's header gives way in the
live Plot's order (`narrowStep`, above): the leading controls keep their
width, the trailing ones give way after Follow's icon and before the axis'
words (`trailingGivesWay`), and Back and Measure stay. Below 400 px of height
the chips keep to one row and scroll.
`WatchTraces::appendRecorded(path, readings)` adds a run's newer readings to
a recorded path without copying the rest. The live Plot is unchanged.

## The Plot's numbers stacked in lanes (November 2026)

New, nothing removed. `WatchTraces.stacked` (L, or "Stack numbers in lanes"
in the graph's menu) draws each plotted number in a lane of its own, one above
another over the bool and enum lanes, with its own values, its leaf name and a
colour bar; it is the Plot's setting, kept by `capture()` and `restore()`
(`"stacked"`) and one step of the new `plot.stack` kind ("Stack the numbers in
lanes", "Put the numbers on one axis"), so `UndoHistory::kinds()` names one
more kind. Stacking needs 40 px a lane: while the Plot is shorter the numbers
share one axis again, and L says "Too many numbers to stack at this height.
Make the Plot taller or remove a curve." `TraceView` gains `stacked`,
`stackFits` and, for checks and hosts, `numberLanes` (each lane's path, rect,
valueRect, nameRect, low, high, labels and cursorY). While stacked, Auto fits
each lane to its own number; a value zoom or pan, a drag or the wheel over the
labels, and a box act on the lane under the pointer (the keys: else the
focused curve's, else every lane); A fits every lane and a double-click on a
lane's labels fits that lane; a click on a lane's name focuses its curve; Back
remembers the lanes' values; `valueAuto` is false while any lane is zoomed by
hand. `TracePlot` puts Auto (A) after the first lane's name while stacked,
and has no "Fitted to" chip then.

## A copy's state, the trees' marks and the two places' words (FL-1e, November 2026)

There are two places, the device and the project, and the Viewer now says
which. `FileViewer`'s footer begins with how the copy stands with the
device's file, then its size, format and path: "Same as the device's · read
12:04" (with the date when not today), "Saved in the project · 3 bytes differ
from the device's", "Written 12:10 · read back the same" or, in the warning
colour, "· 1 byte reads back different", "! The device's file changed after
12:04", "The device's bytes are not known", "Read-only · made by the device's
firmware", each followed by "· not connected" while the copy's device is not.
A file from disk has no state, only its size, format and path, and the
footer no longer says when the copy was loaded. The footer is a `RowLayout`
now (still `fileViewerFooter`, its whole line in `text`, the project's path
and the times in `details`, shown on hover), the state a label of its own,
`fileViewerState`. A change of the state is announced to a screen reader
(`Accessible.announce`); the first state of a file is not.

New on `FileViewer`: `deviceConnected` (the copy's device is the connected
one; follows `reloadable` unless the host sets it), `readOnlyNote` (the few
words after "Read-only · ", the host's), `stateKind`, `stateText`,
`saveCopy()`. New on `FileContent`: `referenceAt` (when the device's bytes
were read or read back), `savedAt` (the copy's last write), `savedDiffers`
(the saved copy against the device's bytes, edits not saved left out),
`deviceChanged` (a write found the device's file changed; until it is read
again; a host may set it), `localUrl()`, the signal `saved()`; on
`OpenFiles`, `fileSaved(localFile)`. `FileContent::Reference` gains `at`, the
time the bytes were read: code that brace-initialises a `Reference` adds it.

The words: Save in the project ("Save descriptor.bin in the project. The
device's file is unchanged."), Read again from the device (was Reload; shown
for a device's file only, "(not connected)" when it cannot), Save a copy as…,
which writes what the tab shows, edits not saved included, through the
viewer's own dialog. It replaces both "Save as…" and "Save the edited bytes
as…": the `⋯` menu no longer emits `saveAsRequested`, which stays declared for
hosts that connect to it, and the menu item `hexSaveCopy` is gone. The hex
state line reads "Saved in the project · 1 byte differs from the device's";
"here", "locally" and "on this computer" read "in the project" in the tool
tips, the discard question and an upload's "nothing in the project records".

`DeviceFilesModel.copyFolder` names the device's folder in the project; each
file's `copyDiffers` then counts the bytes its copy there is saved
differently from the device's bytes as read, with `copyReadAt`, and
`refreshCopies()` compares them again. `DeviceFileRow` shows a `≠` after the
name in the info colour, its words on hover ("The project's copy differs from
this file as read at 12:04: 3 bytes. Open it to write or restore.") and in
the row's spoken name. It adds two required properties, `copyDiffers` and
`copyReadAt`: a host's own model for the row gives both.

`DeviceBytes::compare(localFile, devicePath)` is that comparison, kept until
the copy or a record changes (`DeviceBytes::changed()` says a copy was
written, as `FileContent::save()`, a download and `copyFile` do);
`DeviceFileTransfers::copyState` now reads it. `DeviceBytes::devicePathOf` and
`DeviceFileTransfers::devicePathOf` give the device's path a copy's record
names, so a host opens a copy from its project tree as that device's file -
the same document as from the device. `DeviceFileTransfers::deviceFolderFound`
gives the connected device's folder without making or renaming it.

## A descriptor that is not the connected device's (FL-1f, November 2026)

`FileContent` gains `fingerprint` and `referenceFingerprint` (the descriptor
fingerprints in the copy's header and in the device's bytes as read, "0x" and
16 digits as `DescriptorContent` writes them; `descriptorFingerprint(bytes)`
reads one) and `connectedFingerprint` (the connected device's, from the
codecs' context), with the signal `contextChanged`, which
`OpenFiles::contextChanged()` now sends each file. A descriptor from a device
that is not the connected device's shows "This descriptor is not the
connected device's (3f2a… against 9b10…)" over its view
(`fileViewerOtherDescriptorBar`) with Read again from the device; while its
device is connected and the bytes it was read from are another fingerprint,
the state line says the device's file changed after that read.

`DeviceFileTransfers::keepOtherDescriptor(deviceFolder, versionFolder)` keeps
the project's copy of the descriptor, with its record, as a device version
under `versionFolder` when its fingerprint is not the connected device's -
once for each fingerprint - and leaves the copy itself as it is. A downloaded
descriptor is checked against the one read at connect: one of another
fingerprint ends its row "… · its fingerprint 0x… is not the one read at
connect (0x…): connect again" and keeps it (`descriptorNote`).

## The Watchlist's leftovers of WL-3b and WL-4d (WL-8, November 2026)

- A number out of range is said in the row's form: "“65536” is out of range
  for u16. Enter a number from 0 to 65535 (0x0000 to 0xFFFF)." and, on a row
  shown in hex, "... from 0x0000 to 0xFFFF (0 to 65535)." `watch_format::encode`,
  `fromHex` and `range` take an optional `WatchNumberFormat form` (Original by
  default); the dynamic provider passes the row's own form, or the Watchlist's.
  The sentence changed from "...: enter 0 to 65535 (...)" to the one above.
- An enum's choices in the row editor follow the row's form: on a row shown
  in hex they read "Idle (0x00)". The model now puts the row's own code first
  when no choice is it: by its name when the type has one past the choice
  limit, else "Unknown (5)". The unknown code is "Unknown (5)" everywhere in
  the Watchlist (`watch_format::text`, `WatchlistModel::formatValue`), no
  longer "5 (unknown)". The Inspector's own value text (`DynamicModels`) is
  unchanged.
- Polling resumes by itself when the same device comes back.
  `DynamicWatchlist::resumes()` holds polling the operator started and that
  nothing but a lost link stopped (not `stop()`, `clear()`, or a failure while
  the device is still there); `forgetResume()` drops it. The dynamic provider
  remembers the descriptor fingerprint polling was started on and begins
  polling again when a device with that fingerprint is attached; another
  device forgets it. While away, the status says "polling resumes when this
  device is back"; a read drained after the link went now says the same
  no-device line, not "Stopped - accepted read drained".
- The "+" row works with no device: `addWatchPaths` keeps every well-formed
  path as a row by its path (one step, `offline: true` in the answer) that is
  read on a device that has it; `checkWatchPath` answers "missing" with "No
  device. Enter keeps it; ..." or "watched" for a path kept already.
  `DynamicWatchlist::PathResolution` gains `offline`.
- A group whose name holds dots ("power.meter") is read as the longest known
  group a path starts with, then the Field: `resolvePath`, `pathCandidates`
  ("power." offers "power.meter." and power's own Fields) and a kept row's
  saved binding.
- Undo of rows removed while offline was already done by UR-1 (One history
  for Undo and Redo); nothing changed for it.

## The Plot's header holds its step while a host's words change (November 2026)

`TracePlot`'s header folded and unfolded on every change of a host's words
near a threshold: the Trend's tag in `trailing` ("Recording", "5 reads not
saved", "6 reads not saved"...) or Follow coming and going moved the spans
between "10 s | 1 min | 10 min | All" and "All ▾", and the header row and the
chip row under it jumped on each update. The give-way step now has
hysteresis: it folds at once when the controls no longer fit, and unfolds
only when the Plot's own width changes (and once at completion and 300 ms
after it, as the start-up settles). `narrowNeed` is the new read-only step
the controls need now; `narrowStep` is the step shown and is no longer a
binding, so a host must not assign it. `trailingGivesWay` holds with it, and
`trailingNeedsToGo` is what the controls need now.

## One commit rule for the value editors; the cell editor shows its whole value (November 2026)

Every value editor now follows one rule: the Batch's "+" field, the Batch
item's request and write cells and Details' cells (`ValueCell`), and the
Watchlist's row editor.

- Enter commits, and so does focus going elsewhere: a click away, or Tab.
  Escape cancels and puts back the value the editor opened with.
- Text that is no value is never dropped in silence. Enter keeps refusing it
  as before. On focus loss the old value stays and the editor's own caption
  or status line says why, in Enter's words.
- The text the editor opened with commits nothing: no write, no undo step.

Per editor:

- `ValueCell`: a click away keeps the value as Enter does (unchanged). New:
  closing with text that is no value shows "Not changed: <takes>" in the
  caption under the closed cell (`refusal`) until the cell opens again or is
  `reset()`. `takesText` is `takes`, or "Enter a value of type <type>.". A
  text equal to the row's `editorText` no longer calls `setText`.
- `ValueCell.editorRoom` (new, default 0): how far left of the cell the
  editor may grow over the owner's spare room. While open, the text field is
  its whole text and the cursor wide (TextMetrics in the fixed face), between
  the cell's width and the cell's width plus `editorRoom`. Its right edge
  stays on the cell's, and a text that fits shows from its start. Details
  gives it the Name column's room, short of the name's first characters, so
  "3.14159274" no longer reads ".14159274". The choice combo follows the same
  rule. It is as wide as its widest choice ("false (0)", "Active (0x02)") with
  the arrow, measured over every choice in the list's bold weight, and its
  list is never narrower than that. A host that gives no room keeps the
  combo in the cell when the choices fit.
- Watchlist row editor: focus leaving the editor (a click on another row or
  anywhere else, or Tab past its own buttons) writes to the device, as Enter
  does, under the same Allow changes rule. Text that is no value, or an empty
  one, writes nothing, and the status line (`watchWriteOutcome`) says "Not
  changed: <Enter's refusal>". That line outlives the click's own change of
  row (`writeRefusalLeft`) until the next edit starts. Enter with the text the
  editor opened with now closes it without writing; the Write button still
  sends the value once again.
- Batch "+" field (`PathAddRow` with `enterAdds`): focus leaving it with a
  line that names one endpoint adds it, as Enter does. A line that names
  nothing stays in the field with its line saying why. Escape, with the
  suggestions shut, now empties the field before leaving it. The Watchlist's
  "+" row (no `enterAdds`) is unchanged. A click on a Batch item now takes
  the keys to the list, as a click on a Watchlist row does.

### A Service's Request keeps its rows over the Response; one form for types

- `EndpointPanel`: when a Service's Request stands over its Response (one
  column, a narrow dock such as the Batch item's pane), the Request's minimum
  height is now its rows' whole height: its headings, the table's header and
  every row (`valueTree.contentHeight`). The Response heading starts below the
  last request row and no longer covers the "factor" row of a two-value
  request. Side by side, or with no response, the minimum is three rows as
  before. In a dock too short for both, the panel still overflows at its
  bottom: it does not scroll yet.
- The value models (`DynamicModels`' `ValueModel`, which feeds Details'
  values and a Service's Response) name scalar types in the short form the
  Request and the Device tree already use: `u8 i8 u16 i16 u32 i32 u64 i64
  f32 f64`, and `enum (i16)`. They were `uint8 ... float64` and `enum (int16)`.
  A host that compares the `typeText` role with the long names must use the
  short ones. The Inspector's device check does.

### The Batch item pane scrolls; an edit back to its value is no step

- `EndpointPanel` has a new `scrolls` property (default false). When true its
  content lives in a Flickable with a vertical scroll bar shown as needed, and
  a Service's Request and Response each keep every row's height (one column or
  side by side), so a dock shorter than both scrolls instead of overflowing at
  the bottom. `BatchPanel` sets it on the item pane; Details is unchanged.
  The scrolling area is `endpointBodyScroll`.
- Typing a value and then the value the cell had when it was entered leaves
  no step in the UndoHistory (a step merged back to where it started is
  dropped; `tests/dynamic_batch` now holds this); a real change stays one step.

## Details and the Batch pane as a tree; the Watchlist edits in place; the cell editor closes on Enter (November 2026)

- `EndpointPanel` draws its value tables as a tree: one root per table (a
  section heading, or the Field's own root row, open by default), a chevron and
  indent guides, one indent step per table (16 px, down to 8 px when the
  deepest level needs its 80 px), folded branches that show their values
  (`{Gain 1.5, Enabled true, ...}`, `[0, 0, 1, ... 64 items]`), keys (Left,
  Right, `*`, Enter, Shift+F10) and a row menu (Expand all under, Collapse all
  under, Collapse all). The root row's name is the Field's last path segment;
  a nested struct's name is no longer bold.
- Fold memory: `EndpointPanel.foldMemory` / `foldScope`. Details keeps the folds
  per selected endpoint; `BatchPanel` owns one `FoldMemory` for its life and
  scopes it by item uid. New `FoldMemory.qml` (exported with the module).
- A provider's value models should offer the new `ValueModel` roles: `path`,
  `summaryParts`, `summaryKind`, `itemCount`, `unsentCount`, and `pathOf(index)`,
  `indexOfPath(path)` and `deepest`; a model without them still draws, without
  summaries, memory or the compressed step. A branch's `unsent` is true when any
  value under it is. The "3 children" text is gone from aggregate rows.
- Arrays longer than 128 elements show ranges `[0...127]`, `[128...255]` (each
  folded, fetched when opened) instead of "Load next 128"; their paths are
  `Samples[128...255]`; an element's path stays `Samples[129]`.
- `ValueCell`: Enter closes the editor and leaves nothing selected (the table
  behind it no longer opens the cell again); the "Encoded" caption is one line
  across the row (`captionRoom`, `captionRightRoom`, `restRoom`) with the exact
  value and its bits in its tooltip (`3.1415927410125732 · 0x40490FDB`); the
  Value column's width is measured from the text and follows the pane's width;
  new draft mode (`commitText`, `checkText`, `note`, `noteDetail`,
  `openOnClick`, `listOpensOnBegin`, `changed`, `extraDescription`,
  `editorWidth`).
- `WatchlistPanel` edits in place with `ValueCell`: no two-line editor, no
  "Write all" (`watchValueWrite` is gone); Enter, Tab, a click away or a choice
  writes, Escape and an unchanged text write nothing; a member's write is
  announced on one quiet line ("Writes all 3 members of Config."). Gone object
  names: `watchValueEditor` (now `scalarEditor`), `watchValueChoice`
  (`valueChoice`), `watchValuePreview`, `watchValueError`, `watchWriteScope`,
  `watchValueEditorToolTip`, `watchValueChoiceToolTip`, `watchEditButton`.
- The panel's row-count bumps (`valueModelRevision`, `responseModelRevision`)
  follow `rowsInserted` / `rowsRemoved` one turn later.

### The Plot's later list: Measure tooltips, warnings, pins, gaps, keys

- Measure: the Δ, Mean, Pk-pk and Std dev titles carry a tooltip saying what the
  column is. The range caption ends with "· 2 failed reads" when reads failed in
  the range. A line (`plotMeasureTrimmed`, Theme.warning) says "Readings before
  12:01:02 are no longer kept; measured from there." when a pinned reference is
  older than the readings kept. A Measure row takes focus with Tab, and the Menu
  key (or Shift+F10) opens the chip menu there (Go to minimum / maximum).
- `TraceView::measureRange()` also returns `failed` and, when the pinned range
  starts before the oldest reading kept, `keptFromUtcMs`. New invokable
  `gapCaptionAt(x)` and properties `hoverGap` / `hoverGapX`: a gap band too
  narrow for its caption says "Watchlist stopped 14:14:41" in a tooltip.
- The graph menu groups Pin reference here, Pin end here, Remove end, Remove
  reference (new item `plotMenuUnpinEnd`). "Pin end here" works without a
  reference: it pins one first, as Shift+click does. The menu is taller by two
  separators and an item. `plotSaveDialog`'s title is "Save the values from the
  reference to the end" while both pins are set.
- A bool or enum chip shows no Δ at all with a reference (it showed "Δ –" where
  no reading existed). When the reference and end chips would collide, or would
  not fit beside the axis switch (320 px), one chip, the end's ("Δ 0.4 s ·
  14:14:17.875"), stands for both.

### A dropped struct or member is one Batch item, the Field

- `addBatchPaths` maps every path to the Field that holds it, keeps the first
  appearance of each Field and adds one item per Field: a struct, any of its
  members, or the struct with all its members is one read. A group named as a
  path adds one item per Field in it. Commands and Services stay one item each.
- New `DynamicConfiguratorProvider::batchItemCount(lines)` gives that count;
  the Batch list's drop notice uses it ("Drop to add 1 item to the Batch")
  where it exists and falls back to the number of paths.

### A curve of another run has its own anchor (Trend, From another run)

- `WatchTraces` keeps an anchor per recorded path: `setRecordedAnchor(path, {originNs, utcMs})`
  (a zero anchor takes the path back to the shared one), `recordedAnchor(path)`,
  `shiftNs(path)`. `setRecorded` and `appendRecorded` still take readings as their run
  recorded them; `readings(path)` holds them where the axis puts them, so every reader
  (the cursor, Measure, the reference and end pins, find, the CSV, box zoom, Back, the
  lanes) sees one time base without knowing about anchors. A path with no anchor of its
  own is untouched, so the live Plot, where every curve shares the session anchor, behaves
  exactly as before.
- `setAxisFromStart(bool)` (default false, the Plot's time of day) says how a curve with an
  anchor of its own is placed: counted from its own start, lined up with the run shown, or
  at its own clock time. `TraceView::setFromStart` and `setTraces` set it, so the switch
  at the bottom right moves such curves with no other wiring. Changing it, or `setAnchor`,
  moves the stored readings by the difference in one pass.
- `lastReadUtcMs(path)` gives the time on the curve's own run's clock. The CSV's time of
  day column and Measure's "at" times of a curve of another run are, on "From run start",
  the shown run's clock at the aligned moment; on "Time of day" its own.

## Trend: a data update never undoes the user's time zoom

- No API change. `TracePlot`/`TraceView` state for the time axis (`showAll`, `spanSeconds`,
  the held right edge) is changed only by the user: wheel, drag, box, keys, presets, Back,
  Follow, or a host choosing another run (`choosePreset`). `WatchTraces::setRecorded`,
  `appendRecorded` and `setRecordedAnchor` never touch it.
- qml_layout now holds this: after a wheel zoom from All, on a recording and on an overlaid
  curve an hour away, setting and appending readings again leaves the span and right edge
  as they were, and a step sideways still pans.
- Known, left to the gesture redesign: at All (or a span longer than what is kept) the
  right-edge clamp in `TraceView::hold` leaves a sideways drag nowhere to go.
