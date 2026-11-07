# Mount a Configurator over a Session

The browser view can borrow an owned SDK connection Session directly:

```js
import {connectDynamic, demoWebSocket} from 'telemetry-sdk';
import {mountConfigurator} from 'telemetry-ux/web';

const session = await connectDynamic({transport: demoWebSocket(location.origin)});
const view = mountConfigurator(document.querySelector('#device'), {session});

// Removing a view does not cancel an accepted operation or close the connection.
view.unmount();
await session.close(); // The application owns this decision.
```

`demoWebSocket` selects the existing reference server's bootstrap/lease contract.
It does not infer an arbitrary STM32 server, authentication scheme or safe remote
cleanup from a URL. See the [SDK connection contract](https://github.com/shpegun60/telemetry-sdk/blob/main/doc/ConnectionSession.md).

For Static mode, pass generated context once when connecting:

```js
import {connectStatic, demoWebSocket} from 'telemetry-sdk';
import {mountConfigurator} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {structuralMetadata} from './generated/js/device.metadata.js';

const session = await connectStatic({sdk, metadata: structuralMetadata,
    transport: demoWebSocket(location.origin)});
const view = mountConfigurator(document.querySelector('#device'), {session});
```

Generate this UI context with `--structural-metadata --wire-editing`. Structural
metadata supplies generic UI descriptions, while preservation-aware codecs keep
unchanged NaN payload bits during Field edits. Neither option is required for
ordinary typed reads. A mismatching Static schema refuses connection; there is
no implicit Dynamic fallback. The SDK exposes its generated module as read-only
context, never its internal client or transport backend.

## Independent views and ownership

Each `{session}` mount creates one presentation provider with independent
selection, drafts and response snapshots. All those providers submit through
the same existing Session/client FIFO:

```js
const editor = mountConfigurator(document.querySelector('#left'), {session});
const other = mountConfigurator(document.querySelector('#right'), {session});
```

`mountFieldEditor`, `mountCommandPanel` and `mountServicePanel` accept the same
Session options. An optional `endpoint` selects one known endpoint without
sending a request:

```js
import {mountFieldEditor} from 'telemetry-ux/web';
const field = mountFieldEditor(document.querySelector('#config'), {
    session, endpoint: {category: 'field', catalog: 'control', name: 'Config'}
});
```

The catalog/name above belongs to the reference Model. Use your own names or an
explicit `{category: 1, id: fieldId}` selector. Omitting `endpoint` leaves selection
to the user. An explicit unmatched or ambiguous selector fails before mounting a
ready Session; when mounted while disconnected, it resolves on readiness and
reports a mismatch through provider state. An initial selection is restored only for
an unchanged schema fingerprint; changed schemas require explicit selection.
Mounting never reads, writes or executes automatically.

`view.provider` exposes the local provider for application controls. Applications
that manage providers explicitly can instead call `createSessionProvider()` and
mount `{provider}`. Such mounts borrow the supplied provider; `unmount()` leaves
it alive. Call its `dispose()` when its presentation ownership ends. Supplying
both a Session and a provider is an error.

`dispose()` permanently detaches lifecycle observation and hides this provider's
values. It never disconnects the shared Session, cancels its accepted requests or
deletes their terminal operation facts. Keep the provider reference if the host
needs to inspect a pending operation's historical completion after removing a view.

## Lifecycle and freshness

The provider observes Session state automatically. Revocation hides values and
drafts immediately; readiness rebuilds metadata from the current schema. A result
that belongs to an old connection can retain `Accepted`, confirmed success or
`OutcomeUnknown` while its value remains unavailable. Standalone panels display
these historical facts even when current selection has been cleared.

The same adapter exposes Watchlist and Mixed Batch through the tracked high-level
Session operations. Discovered TRM1 and TBM1 records are copied independently for
each new admission. On the reference profile, enable the corresponding backend
operations explicitly when creating the transport:

```js
const transport = demoWebSocket(location.origin,
    {enableReadMany: true, enableBatch: true});
const session = await connectDynamic({transport});
const watch = mountWatchlist(document.querySelector('#watch'), {session});
const batch = mountBatchEditor(document.querySelector('#batch'), {session});
```

Import the two mount functions from `telemetry-ux/web`. Each mount has its own
watch selection or ordered Batch recipe and submits through the same Session
FIFO. Polling has one pending cycle, skips missed ticks and stops on invalidation.
Reconnect does not restore or execute a recipe, restart polling or replay an
uncertain mutation. ReadMany requires a supported scalar/enum selection; the user
can explicitly choose Individual FieldRead instead. Missing capability or backend
support reports a refusal and never causes hidden fan-out. A mixed Batch has
per-item outcomes, without rollback, atomic snapshot or exactly-once guarantees.

A FIFO-admitted mutation through a Session provider makes sibling providers' cached
values and responses stale. Dirty draft text remains visible for review, but it
cannot be edited or submitted until discarded and prepared again. The view shows
the reason; it does not silently overwrite another panel's draft. A refused
submission such as `QueueFull` does not invalidate sibling state. A later device
refusal does not restore invalidated data; this policy is deliberately conservative.
Pending checked operation facts remain available, and an older foreground,
Watchlist or Batch read cannot republish its stale value. A read-only Batch does
not invalidate sibling data. Accepted mutation items do, even when their eventual
endpoint result is a refusal or their final outcome is unknown.

This is a local presentation policy, not server-side compare-and-write or an
atomic snapshot guarantee. Direct headless calls through the Session and changes
from other applications are outside that managed-view notification path. After
such work, call `view.provider.invalidateData(reason)` to invalidate the managed
providers, then explicitly refresh. No automatic mutation replay or background
refresh is added. Write followed by readback remains two ordinary operations.

`snapshot().connection` exposes connection state and diagnostics independently
of a provider's local editing error. UI code does not need to call `sync()` or
`invalidate()` for ordinary Session lifecycle transitions.

See the [two-view reference example](../examples/session_web/README.md) and
[installed consumer tests](../tests/session_provider/README.md).
