# Generated typed Configurator provider

Generate the opt-in binding with the installed matching SDK and UX Python packages:

```sh
telemetry-codegen generate Device.descriptor --output generated \
  --targets cpp,qml --structural-metadata --ux-starter qml
```

The generated `qml/ConfiguratorBinding.hpp` adapts the existing generated API to
`telemetry::ux::StaticConfiguratorProvider`. The host's CMake project calls
`find_package(TelemetryUX 0.1.0 EXACT REQUIRED COMPONENTS static_configurator)`
before adding the generated QML subdirectory, and links
`TelemetryUX::static_configurator` with its generated API target. Install prefixes
belong in the host's normal `CMAKE_PREFIX_PATH`; no source checkout or manual QML
engine import path is required.

```cpp
#include <ConfiguratorBinding.hpp>
#include <Telemetry/UX/StaticConfiguratorProvider.hpp>

// Api, its adapter, backend and transport are already supplied by the host.
telemetry::ux::StaticConfiguratorProvider provider{
    device_api::qml::configuratorBinding(api)};
provider.setAllowMutations(true);
engine.setInitialProperties({{"deviceProvider", QVariant::fromValue(&provider)}});
```

```qml
import Telemetry.UX

Configurator {
    provider: host.deviceProvider
}
```

`host` is the application's root object with a `deviceProvider` property.
Mutations default to disabled; permission belongs to the current admitted session.
Disconnect and session replacement clear `allowMutations`; enabling it while
disconnected is refused. The host must explicitly opt in again after admission.
Ordinary cache notifications in the same session preserve the setting. The static
provider supports one foreground Field/Command/Service workflow and explicit
Individual FieldRead polling. The existing dynamic Inspector remains another
provider of the same view.

## Static Watchlist

Select a readable scalar Field or enum and call `addWatch()` (at most 32 distinct
Fields). `refreshWatch()` performs one sequential cycle; `startWatch()` repeats
cycles at `watchIntervalMs` (100–60,000 ms). `stopWatch()` stops future reads and
drains an already accepted read. Membership and interval changes are refused
until the cycle and accepted read have ended. A slow read or foreground operation
causes timer ticks to be skipped; polling never accumulates another request queue.
Selection and mounting the QML view do not submit work.

This provider currently exposes `watchIndividual == true` and
`watchReadManySupported == false`. It does not silently turn a ReadMany request
into several reads. Actual static QML ReadMany requires separate generated API
and adapter capability support; headless SDK ReadMany alone does not provide it.

Each row projects the existing generated Field cache as exact display text, with
its own status and session freshness. U64/S64 never pass through a QML Number;
unknown enum codes remain decimal text. Failed reads publish an empty value and
an error status, even when the generated API still retains an older cache value.
The Watchlist owns no additional confirmed DTO or wire payload. Successful
external cache notifications may update the corresponding projected value.

Foreground reads serialize with the current cycle. Accepted foreground mutations
stop polling and invalidate Watchlist presentation; restart explicitly after the
operation. A session change immediately invalidates values and stops polling.
Membership is retained for the same generated schema, but a later admission never
restarts polling or publishes an old callback. The host still owns transport
recovery and physical quiescence.

## Ownership and lifetime

The host owns the generated `DeviceApi`, its one `DeviceClientAdapter`/FIFO,
backend, transport and owner loop. Keep them alive while the provider is in use,
and keep the provider alive while its view uses it. All provider actions and Qt
model notifications occur on that owner thread. Removing the view/provider does
not cancel, reset or close host work.

Confirmed Field values stay in the generated API's cache. The provider projects
that cache without another confirmed snapshot. Editing owns a QVariant draft and
its original canonical Field bytes. Service response presentation owns a QVariant
copy before completion returns; it does not retain an RX view. Qt may allocate
for its models, strings and implicitly shared values; this is a host-only module.
The SDK's standalone preserving C++ codec remains allocation-free and independent
of Qt or this package.

Use normal Qt deferred destruction (`deleteLater`) when responding to structural
model notifications such as `rowsAboutToBeInserted` or `modelAboutToBeReset`.
The provider and models must survive the corresponding Qt structural call.
Direct destruction at the provider's completed `stateChanged`/`readFinished`
boundary is supported. See [QObject lifetime rules](https://doc.qt.io/qt-6/qobject.html#dtor.QObject).
Old or foreign QModelIndex values cannot access a replacement value; handles do
not survive destruction of their model.

## Editing and results

The Field workflow is **Read → Edit → Apply → Readback**. A preserving edit needs
a confirmed readable Field and a private original seed. Write-only Fields are
discoverable but have no read-based editor here; a specialized host can use the
ordinary generated write method. Unknown enum codes and U64/S64 remain exact
decimal text. Invalid/intermediate input stays visible and blocks submission;
it does not replace the last valid draft. Unchanged F32/F64 NaNs retain original
sign, signaling and payload bits. This is not a server compare-and-write or
protection against another application's concurrent updates.

Accepted Write hides the prior displayed Field value. Successful Write starts
one separate FieldRead; confirmed Write and failed readback remain distinct.
QueueFull/Busy restores the preceding draft and presentation freshness state.
Failed Refresh retires the displayed confirmed value without modifying the host
cache. An existing local draft retains its known original base until canceled or
submitted.

Commands retain `Accepted` versus `Executed`. Service refusals display their
application status, and Void response differs from a real empty Struct. No
failure automatically repeats a mutation. `OutcomeUnknown` retires the draft and
blocks new provider mutations for that session; the host performs the existing
SDK recovery and new admission lifecycle. Schema/session changes clear values,
drafts and response presentation; old operation facts remain labeled historical.
Results from an earlier session cannot publish a new value.

Selection and request initialization preflight a maximum 65,536 presentation
nodes and depth 64. Arrays materialize at most 128 immediate Qt rows per fetch.
Initialization and wire validation delegate to generated typed code; this
provider has no Descriptor parser or hand-written payload encoder. One queued
notification may coalesce reentrant Qt presentation changes; it never advances
the client. Individual Watchlist cycles schedule their next explicit FieldRead
on a later owner turn, with at most one accepted read at a time.
