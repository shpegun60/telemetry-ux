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
| `connected`, `busy`, `editing` | Connection/operation/draft state |
| `canEdit`, `canApply`, `canRefresh` | Host-authorized Field actions |
| `commandSelected`, `canExecute`, `serviceSelected`, `canCallService` | Operation selection and action gates |
| `watchActive`, `watchPending`, `canAddWatch`, `watchCount` | Watchlist state and gates |
| `watchStatus`, `writeOutcome`, `commandOutcome`, `serviceOutcome`, `statusText` | Already-classified display text; the view does not infer execution certainty |
| `watchIndividual`, `watchIntervalMs` | Writable watch mode and polling interval; the provider validates changes |

Actions retain the existing provider signatures:

- `selectEndpoint(QModelIndex)`, `refresh()`, `beginEdit()`, `cancelEdit()`,
  `apply()`, `executeCommand()` and `callService()`.
- `setScalarText(QModelIndex, QString) -> bool` validates raw editor text. The
  view preserves invalid/intermediate text and uses the result to gate its input.
- `addWatch() -> bool`, `removeWatch(int)`, `clearWatch()`, `refreshWatch()`,
  `startWatch()` and `stopWatch()`.

The view consumes `readFinished(bool published)` and model `modelReset()`
signals to restore the first visible expansion. Models support their normal Qt
index/fetchMore interface. Endpoint roles are `name`, `detail`, `isEndpoint`.
Value roles are `name`, `detail`, `value`, `isAggregate`, `childCount`,
`loadedCount`, `editable`, `editorText`. Responses use the same roles except
`editable`/`editorText`; watchlist rows expose `name`, `status`, `text` (ListView
supplies `index`). Scalar display/editor values are strings, including U64/S64
and enum codes; the view never converts them through JavaScript Number.

All model/provider display strings and tooltip details are rendered as plain
text. Names or enum labels containing markup remain literal data and cannot
request embedded images. A host that needs rich content supplies it in its own
custom panel rather than embedding HTML in these model roles.

The host serializes actions and model notifications on its Qt owner thread,
keeps the provider alive while the view or its deferred updates exist, and
implements all lifecycle/permission/error rules. Buttons forward their action to that provider; text can also be revalidated
on focus loss. The view supplies no automatic mutation replay or exactly-once
guarantee. Existing lazy array fetching remains limited to 128 immediate rows.
