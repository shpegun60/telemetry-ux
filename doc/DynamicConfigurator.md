# Independent Dynamic QML panels

`TelemetryUX::dynamic_configurator` installs a runtime provider and the same Qt
models used by the Inspector. It requires an admitted native Workbench; the host
continues to own connection/bootstrap, its one DynamicClient/FIFO and progress.
It does not use the JavaScript Session or introduce a native connection manager.

## Build and embed

Install the matching SDK with `workbench` plus the components your transport uses.
Build UX with the provider explicitly enabled; pure Qt/static installations do
not require the SDK:

```sh
cmake -S /path/to/telemetry-ux -B /external/ux-build -G Ninja \
  -DTELEMETRY_UX_BUILD_INSPECTOR=OFF \
  -DTELEMETRY_UX_BUILD_DYNAMIC_CONFIGURATOR=ON \
  -DCMAKE_PREFIX_PATH="/path/to/Qt;/external/sdk-prefix" \
  -DCMAKE_INSTALL_PREFIX=/external/ux-prefix
cmake --build /external/ux-build --target install
```

Your application links installed targets, without adding Inspector sources:

```cmake
find_package(TelemetryUX 0.1.0 CONFIG REQUIRED COMPONENTS dynamic_configurator)
target_link_libraries(my_app PRIVATE TelemetryUX::dynamic_configurator)
```

Create one binding after native admission, then one provider per independent
panel. The binding takes a `shared_ptr<void>` token retaining the Workbench,
Descriptor, workspace and backing buffers. In the following integration snippet
`state` is the application's already admitted shared native state:

```cpp
#include <Telemetry/UX/DynamicConfiguratorProvider.hpp>

telemetry::ux::DynamicConfiguratorBinding views{state->workbench, state};
telemetry::ux::DynamicConfiguratorProvider config{&views};
telemetry::ux::DynamicConfiguratorProvider monitor{&views};
engine.rootContext()->setContextProperty("configProvider", &config);
engine.rootContext()->setContextProperty("monitorProvider", &monitor);
```

```qml
import QtQuick
import QtQuick.Layouts
import Telemetry.UX

RowLayout {
    Configurator { provider: configProvider; Layout.fillWidth: true; Layout.fillHeight: true }
    FieldEditor { provider: monitorProvider; Layout.fillWidth: true; Layout.fillHeight: true }
}
```

The [runnable installed-package example](../examples/dynamic_qml/README.md) supplies
a complete local native owner/Model and the owner loop, not just these snippets.
For the shipped Inspector, `createConfigurator()` creates another independent
provider over its existing connection and follows its State replacement.

## Ownership and progress

All calls, native progress and Qt notifications run on one owner thread. After
progressing the existing Workbench/transport, call `views.poll()`. This only
publishes already completed results and reclaims retired local views; it never
drives the core, retries, polls endpoints, opens a socket or starts a timer.

Each provider owns independent Qt models, selection, edit errors and outcomes.
Its native view owns independent snapshot/draft/committed-request state and
service response storage. Construction and selection issue no endpoint requests.
Request capacity and request identity remain global to the one native client.

Deleting a provider detaches its presentation and retires its native view. The
binding retains that view and its buffers until native callbacks have returned.
Removal does not close the owner, cancel another panel or replay a mutation.
The binding emits `operationFinished(QVariantMap)` once after native progress,
including for a removed panel. Observe that signal at the host if completion
facts must outlive individual windows. It contains the operation, client status,
dispatch/endpoint status, session/endpoint identity, exact sequence text and
accepted/executed flags. Service also reports publication/value status; Apply
reports readback facts. These are operation facts, not borrowed DTO views.

Before replacing a connection or its Descriptor:

1. `views.retire()` disables local submissions/publication.
2. Complete the owner's existing close/drain and physical/remote recovery rules.
3. `views.poll()` publishes terminal operation facts outside the native callback.
4. Require `views.drained()` before destroying the binding or its native backing.
5. Create a new binding after new admission; call `provider.setBinding(&newViews)`
   on retained providers. No old request is transferred to that admission.

`drained()` is a local native-view lifetime fence. It is not proof of driver/DMA
release or remote completion. Pending work cannot be released simply by deleting
the binding; destroying an undrained binding is a contract violation. Portable
bare-metal `WorkbenchView` has the same explicit retire/drain lifetime rule and
needs no Qt, `shared_ptr` or heap. Shared ownership here is confined to UX.

## Editing and freshness

Mutations require both host policy (`views.setMutationsAllowed(true)`) and explicit
panel opt-in (`provider.setAllowMutations(true)`), as well as the endpoint's actual
capabilities. Read-only TCP/UART Inspector profiles cannot enable writes merely
because Descriptor metadata says a Field is writable.

The provider edits through the existing RequestEditor and canonical writer.
Untouched NaN payloads and signed zero remain original wire bytes. U64/S64 and
unknown enum values are rendered as exact decimal text, not QML Number. Invalid
input leaves the native draft unchanged and blocks submission until corrected.
Apply keeps Write and its separate fresh FieldRead outcome distinct. Accepted
Command does not mean Executed. OutcomeUnknown is retained without automatic
replay; reconnect does not execute old work.

A managed accepted mutation immediately invalidates sibling cached data/editor
leases, without revoking admission or cancelling accepted operations. Rejected
submissions such as QueueFull do not invalidate siblings or discard the origin
draft. Read again explicitly, or reselect Command/Service to prepare fresh
arguments. The selection remains available after data invalidation. For direct
headless owner mutations, call `views.invalidateData()` after accepted submission;
the Inspector bridge coordinates its own foreground writes automatically.

Historical outcomes stay visible after revocation/replacement and are marked
`previous session`; stale values do not become current after reconnect. Service
response bytes are copied by the native store before the user callback returns.

## Current bounds

This component uses an explicit native profile: 8 shared FIFO slots, 4096 request
bytes, 65536 snapshot bytes and a 1536-byte callback slot. Each panel additionally
owns 65536 Service response bytes and 65536 bytes for the selected Batch result
snapshot. Captured Batch requests and owned per-item response payloads are
bounded by the advertised limits and the 4096/65536-byte Session profile.
These are opt-in host UI budgets, not changes to the
portable defaults; memory grows with the number of panels. Qt model labels and
rows use normal host allocations. Arrays materialize at most 128 children per
fetch. Native-only providers omit Watchlist controls. Providers created by
`QtConnectionBinding` additionally expose the Session-backed Watchlist described
below and the explicit Batch list described after it.

## Session-backed Watchlist

Create providers through `QtConnectionBinding::createProvider()` to use the
existing Configurator Watchlist. Select a readable scalar Field or enum and
press **Add selected Field**. Membership and selection perform no I/O. Each
panel owns its names, values and watch selection while all panels share the
Session's single Workbench, client and FIFO.

The endpoint tree also supports marking several readable Fields independently
of the Field open in the editor, then adding them together. Adding does not
start a read or change the selected Field or its draft. Scalar Fields and
enums can be watched; duplicate and unsupported choices are reported and
skipped. An open draft on the same provider (a Field edit or a Command or
Service request) does not hold the Watchlist: reads never touch it. Writes,
Commands, Services and Batch run while it polls, from this panel as from an
independent one on the same Session and FIFO.

The optional Qt provider API `addWatchMany(QVariantList sourceIndexes)` accepts
indexes from that provider's `endpoints` model. Its result map has `status`
(`ok`, `refused`, `invalid`, or `stale`) and counts `added`, `duplicate`,
`unsupported`, and `capacity`. `ok` can include skipped items; other statuses
publish none. A successful group is published in one Qt model change. All
indexes and the session must still be current at the publication boundary;
retirement after that boundary is drained normally. Static schema navigation
is available offline, but adding watches requires an admitted connection.
The 32-Field bound applies to a ReadMany request, not the watch list.

**Read once** reads the selected list sequentially. ReadMany uses at most 32
Fields in one exchange and obeys both advertised request and response byte
limits; a list of 65 U64 Fields normally needs 32 + 32 + 1. The UI list holds at
most 1024 Fields (32 ReadMany portions per cycle); adding past it reports
`capacity`.
The list is not an atomic measurement snapshot.

**Start polling** accepts intervals from 0 to 60000 milliseconds. This is a
nominal scheduling interval, not a promised device update rate. 0 reads
continuously: each cycle follows the last, still one portion at a time and only
while nothing else is in flight, so other requests always go first. A slow exchange
or busy shared Session skips ticks instead of appending queued poll requests.
While another request of this panel is in flight the Watchlist sends nothing.
After a write the written Field is read again at once when this cycle already
read it (or had it in flight); after a Command, Service or Batch the next cycle
follows this one immediately rather than on the timer.
Each panel retains at most one accepted watch request. Stop disables future
submissions immediately and drains accepted work; membership, mode and timing
remain locked until that completion. A read does not replace the panel's
selected Field snapshot, request draft or Service response.

ReadMany requires successful capability/session admission. An unavailable or
too-small ReadMany capability reports the reason; select **Individual FieldRead**
explicitly to perform separate snapshot-preserving reads. The provider never
silently fans out a failed ReadMany request. Neither mode submits mutations.

Callbacks copy scalar values and per-item statuses into Qt-owned strings before
returning. Model updates happen only after native progress has returned. U64,
S64 and unknown enum values retain their exact decimal representation; the
shared scalar formatter also retains NaN payload bits and signed zero. Session
retirement clears old displayed values, retains accepted callback storage until
drain and starts no old watch cycle after new admission. Start again explicitly.

The host-only `WatchlistSource` seam keeps optional native-only providers free of
Qt Network dependencies. The Session adapter supplies it; it does not create a
client, transport, admission loop or second FIFO. The Inspector and Session
provider reuse the same Watchlist row model and scalar formatter.

[Controlled scheduling checks](../tests/dynamic_watchlist/README.md) cover chunk
and byte budgets, explicit fallback, busy ticks, drain, stale results and model
reentrancy. These checks do not qualify physical transport timing.

[Installed three-panel lifecycle tests](../tests/dynamic_qml_configurator/README.md)
exercise real Encoded dispatch, deletion in flight, new admission, no replay,
exact scalar bytes, owned responses and strict QML warnings. Host/sanitizer tests
do not establish physical STM32 transport qualification.

## Session-backed Mixed Batch

Providers created through `QtConnectionBinding` expose the same optional TBM1
capability admitted by their Session. The public Configurator shows one ordered
list with at most 32 operations. Repeated endpoint IDs are allowed, so a write
followed by a read is a natural sequence. There is no atomic snapshot,
transaction rollback, exactly-once execution or automatic replay guarantee.

1. Select an endpoint in the existing endpoint tree. For a Field write, read it
   explicitly, press **Edit**, and prepare the values in the existing editor.
   Commands and Services use their existing request editor, including Void.
2. Choose `read`, `write`, `command` or `service`, then **Add selected draft**.
   This captures owned canonical request bytes without sending anything or
   changing the editor. A Field read needs no draft. After capturing a Field
   write, **Cancel** closes the local editor before selecting another endpoint;
   the captured Batch step remains intact.
3. Prepare and add more operations. Use Up, Down, Remove or Clear while idle.
   Later editor changes do not alter a captured step. To change its arguments,
   prepare a replacement and remove/re-add that step.
4. Press **Execute Batch** to submit one ordered request through the existing
   Session/FIFO. Any mutating item requires the shared mutation permission and
   this panel's `allowMutations`. A read-only Batch needs no mutation permission.

The entire list must fit the advertised item and byte limits and the Session
profile: request `2 + sum(8 + requestBytes)`, maximum reply
`2 + sum(12 + responseBytes)`. No hidden split or fallback is performed. The list
is locked while one Batch is pending. Execute permits a still-open local draft;
an accepted mutation invalidates it and cached data in sibling panels and the
Watchlist. A rejected submission, including QueueFull, preserves drafts, captured
arguments and previous owned results. Batch writes do not trigger implicit
Field readback; add a Field read explicitly when it is needed.

Every checked item shows its own outcome. Accepted remains distinct from
Executed, and a Service refusal remains distinct from a successful outer reply.
**View result** selects an owned FieldRead/Service value in the existing lazy
read-only value model, including structured responses. Invalid or stale values
are suppressed independently from the checked operation facts. A missing outer
reply identifies no executed prefix: rows retain no fabricated per-item success.
OutcomeUnknown and known historical outcomes remain visible after disconnect;
old values and old-session recipes cannot become current or run automatically.

The provider exposes `batchSupported`, `batchItems`, `batchCount`, `batchPending`,
`canExecuteBatch`, `batchStatus`, `batchSelection`, `batchResponses` and
`batchResponseEndpoint`. Its invokables are `canAddBatch(operation)`,
`addBatch(operation)`, `removeBatch(row)`, `moveBatch(row, delta)` (delta -1/+1),
`clearBatch()`, `selectBatch(row)` and `executeBatch()`. Operation names are the
four lowercase strings above. `batchFinished()` is emitted after native progress
returns; it does not imply that every item succeeded. The row model provides
`name`, `operation`, `endpointId`, `status`, `hasValue`, `historical`, and a
`result` map containing checked native status facts. That map is empty when no
checked per-item outcome exists.

The native callback only copies facts and bounded bytes into an owned completion
slot. Qt model updates and signals occur later at the Session progress boundary.
`BatchSource` is a host adapter seam, not another client or execution queue;
native-only providers without it report unsupported. Controlled presentation
checks are documented in [tests/dynamic_batch](../tests/dynamic_batch/README.md).
