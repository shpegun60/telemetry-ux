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

The Watchlist and the rest of the panel work together. While it polls, selecting
an endpoint, refreshing Details, editing, Apply, Commands, Services and Batch all
work; only adding or removing rows waits for polling to stop. An open draft
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

`writeWatch(row, text)` writes a watched Field from its row, as Details would: its
own read, an exact draft of `text`, the write and a confirming read. It runs on a
second view of the provider, so it never moves the Details selection or touches
its draft (the Details snapshot is marked stale, as after any write). It is
refused without permission (`canWriteWatch` false), for a read-only Field, or
while another row write is in flight (`watchWriting`). `watchWriteOutcome` says
what happened in words (confirmed, written but unconfirmed, an invalid text, the
device's refusal), `watchWriteFinished(confirmed)` signals the end, and the
Watchlist reads the row again at once: within the running cycle, or as a Read
once when it is not polling. Start during a Read once turns that cycle into the
first one of polling.

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
