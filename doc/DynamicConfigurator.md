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
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS dynamic_configurator)
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
bytes, 65536 snapshot bytes and a 768-byte callback slot. Each panel additionally
owns 65536 response bytes. These are opt-in host UI budgets, not changes to the
portable defaults; memory grows with the number of panels. Qt model labels and
rows use normal host allocations. Arrays materialize at most 128 children per
fetch. Native-only providers omit Watchlist controls. Providers created by
`QtConnectionBinding` additionally expose the Session-backed Watchlist described
below. Batch presentation remains separate.

## Session-backed Watchlist

Create providers through `QtConnectionBinding::createProvider()` to use the
existing Configurator Watchlist. Select a readable scalar Field or enum and
press **Add selected Field**. Membership and selection perform no I/O. Each
panel owns its names, values and watch selection while all panels share the
Session's single Workbench, client and FIFO.

**Read once** reads the selected list sequentially. ReadMany uses at most 32
Fields in one exchange and obeys both advertised request and response byte
limits; a list of 65 U64 Fields normally needs 32 + 32 + 1. The UI list has no
32-item limit and grows with available host memory, up to Qt's row-index limit.
The list is not an atomic measurement snapshot.

**Start polling** accepts intervals from 1 to 60000 milliseconds. This is a
nominal scheduling interval, not a promised device update rate. A slow exchange
or busy shared Session skips ticks instead of appending queued poll requests.
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
