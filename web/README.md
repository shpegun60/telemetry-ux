# Web presentation provider

`Provider.js` adapts one host-owned Telemetry SDK client to the reusable web
Configurator. It owns presentation values and editable drafts. The supplied SDK
client owns submission, its single FIFO, payload encoding, timeouts and recovery.
The provider does not open a connection, drive `process()`, retry an operation,
poll, or close the client.

Use `createDynamicProvider({client})` with an already admitted SDK DynamicClient.
Use `createStaticProvider({client, metadata: structuralMetadata, sdk})` with an
already admitted DeviceClient, generated `device.js` exports and its matching
opt-in `device.metadata.js` module. The generated module fingerprint must match
the metadata. Both providers expose the same `PresentationProvider` contract in
[Provider.d.ts](Provider.d.ts); neither provider constructs another client.
Generate editable static Fields with `--wire-editing`; `--ux-starter web`
enables this SDK helper automatically. Older generated modules remain readable,
but applying a Field without the preserving helper reports `Unsupported` rather
than silently re-encoding untouched floating-point values.

Subscribe after creating the provider, then render `provider.snapshot()` once.
`subscribe(listener)` returns an unsubscribe function and subsequently delivers
immutable state. Exceptions from one subscriber do not suppress other
subscribers. A subscriber may invalidate the provider; other synchronous state
mutations from a subscriber are refused to keep publication bounded.

Select an endpoint key from `snapshot().endpoints`. `refresh()` reads a Field.
After a successful read, `beginEdit()` creates a private draft. Command and
Service selection prepares canonical zero arguments, including the distinct
Void and empty-structure cases. These zero arguments are initial editor values,
not device defaults. Writes, Command and Service submission require the host to
opt in with `setWritesEnabled(true)`; this UI setting is not an authorization
mechanism on the device.

`rows(path, {source, offset, limit})` returns direct children of a structure or
array, or one scalar row. The maximum window is 128 rows. An expanded nested row
is read using its returned path. Sources are `value`, `draft` and `response`.
There is no DTO, mutable array, payload or callback in public state. Enum codes
and U64/S64 values remain exact decimal text; open enum values are accepted by
the existing SDK codec. Boolean input is `true` or `false`; integers use decimal
text without a JavaScript Number intermediate for U64/S64. Signed zero is shown
as `-0`. JavaScript numeric values do not preserve arbitrary NaN payload bits.
For Field editing, the provider privately owns the checked original payload and
passes it to the SDK's preserving write operation. The SDK retains original wire
bytes for unchanged leaves, including signed signaling NaNs, while explicitly
changed values use the existing canonical encoder. The provider does not encode
or patch floating-point bytes itself.
A NaN-to-NaN edit retains the seed representation; numeric text does not select
NaN payload bits.

Forward each raw input string with its row path and the revision captured when
that live row was rendered to `setText(path, text, revision)`. Retire old editors
when the selection or session changes; never attach the latest global revision
to an input from an earlier row. Invalid or intermediate text remains visible;
the previous canonical draft is retained and Apply/Execute/Call are disabled
until the errors are resolved. Canonical validation uses the existing generated
codec or SDK `encodeValue`, not a second payload codec. A stale revision refuses
the edit. Actions return a synchronous boolean indicating whether they started;
completion arrives through state subscription. A submission with request ID zero
retains the preceding snapshot and draft.
The original Field payload stays with its draft even if a later Refresh obtains
a newer value. This is the draft's known base, not a server-side compare-and-swap
or a guarantee against concurrent device changes. Cancel, accepted Write,
selection changes and invalidation retire the corresponding private seed.

Accepted Write retires the old Field value and draft. A confirmed Write is
followed by a separate FieldRead. The `write` and `readback` outcomes remain
separate, including a confirmed Write followed by a failed readback. Service
responses are privately copied before publication. Command `Accepted` remains
distinct from `Executed`; neither `OutcomeUnknown` nor any other failure causes
automatic replay.

Each outcome retains its raw SDK status, dispatch and endpoint status. `name` is
the SDK client status name, while `display` describes the application or routing
outcome. For example, a checked Service refusal can have `name: 'Ok'` and
`display: 'InvalidArgument'`; it still has `succeeded: false`.

The host must call `provider.invalidate()` **before** revoking, resetting or
closing the SDK client, and `provider.sync()` after a new admission. Invalidation
retires values and drafts immediately. Calling `sync()` cannot make that same
explicitly revoked session current again. Completion still preserves known
operation facts, with `Stale` value interpretation and historical session labels;
it cannot publish values into a different session. Calling `sync()` also detects
an already revoked client, but host notification is required for immediate DOM
notification because the SDK does not expose a presentation subscription.

Presentation traversal has explicit configurable `limits`: by default 65,536
expanded nodes, depth 64 and 1,048,576 wire bytes for one selected value. Type
preflight happens before value initialization or copying, including zero-byte
arrays. Exceeding a limit reports an editor error instead of allocating the DTO.
Rows remain lazy even though the SDK and provider own a complete selected value.
The supplied metadata and generated codec module are trusted application code;
the static adapter is not a replacement Descriptor validator.

Unmounting the renderer only removes presentation subscriptions and DOM. The
host retains responsibility for every client, transport and pending operation.

## Declarative HTML element

With the host's normal npm/import-map resolution and the existing
`Configurator.css` loaded, use the same provider with a custom element:

```html
<telemetry-configurator id="device-settings"></telemetry-configurator>
```

```js
import {defineConfiguratorElement} from 'telemetry-ux/web';

defineConfiguratorElement();
document.querySelector('telemetry-configurator').provider = provider;
```

`provider` is the host's already-created Static or Dynamic presentation provider.
It is a JavaScript property, not an HTML attribute or a connection URL. The
default tag has TypeScript DOM typing. A custom name can use the returned
constructor: `const Settings = defineConfiguratorElement('device-settings')`.
Registration is explicit; importing the package works in Node without DOM
globals. Calling registration requires a browser DOM. Repeating a known
registration returns the same constructor; an existing foreign registration is
refused.

The element mounts the existing renderer only while connected and supplied with
a provider. Assigning a provider before connection or before registration is
supported. Reassigning the same provider does no work. An invalid provider shape
throws before replacing the preceding view; assigning `null` removes that view.
Removing the element unsubscribes its presentation, retains its provider, and
reconnecting mounts one fresh view. Application-owned light-DOM children remain
intact, so host panels and the existing stylesheet keep working.

Normal assignment and connection mount synchronously. If a custom provider's
subscribe/unsubscribe callback removes the element or replaces its provider, a
retired candidate is cleaned up and the latest presentation is reconciled in a
coalesced microtask follow-up. At most one follow-up is pending; there is no polling timer
or SDK operation queue. The element never invokes host close, invalidate,
cancel, transport or operation methods as part of its lifecycle. The host keeps
responsibility for client admission, processing, pending work and recovery, just
as with `mountConfigurator`.
