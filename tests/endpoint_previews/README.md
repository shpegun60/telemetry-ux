# Endpoint metadata and lazy previews

The Dynamic Qt connection provider exposes device metadata immediately after
admission. `EndpointTreePanel` adds occasional, read-only previews for readable
Fields whose rows are actually visible in an expanded branch. It uses the same
Session, DynamicClient and FIFO as Details, Watchlist and Batch.

Applications using `QtConnectionBinding::createProvider()` do not construct
another client or a preview scheduler:

```cpp
telemetry::integration::qt::ConnectionSession connection;
telemetry::ux::QtConnectionBinding presentation{&connection};
auto* device = presentation.createProvider();
// Expose device to QML as usual, then use EndpointTreePanel { provider: device }.
```

The reusable panel replaces its demand through
`provider.setPreviewDemand(view, sourceIndexes)` and removes it through
`provider.clearPreviewDemand(view)`. A custom view must do the same when hidden,
collapsed, scrolled or destroyed. Demand contains current source indexes, not
remembered row numbers. Field ID zero is valid. A child requests its owning
Field; a nested member or array element is never a separate endpoint.

## Metadata and values are separate

`EndpointModel` supplies `typeText`, `accessText`, `unit`, `wireBytes`, `typeId`
and `responseTypeText`. Its endpoint map also includes the Service response type
ID and size. Types describe wire data, not native C++ class names. A Command's
request type and a Service's request/response types do not execute either
operation.

`selectionInfo`, `definitions` and `definitionRootIndex` let Details show the
selected Field's schema before Read. Expanding a structure or array only creates
local metadata rows. Values stay empty until a successful read exists. A single
Field read supplies the complete structure; its children read from the owned
payload without further requests.

The model exposes `previewEligible`, `previewFieldId`, `previewAggregate`,
`previewChild`, `previewValue`, `previewState`, `previewAgeMs` and `previewReason`.
`previewValue` is backend-formatted text; `unit` is separate. A current model
index is required for `previewDemandField(index)`; an invalid result means no
eligible demand, while numeric zero is a real Field ID. Preview-only role
notifications can be recognized by `previewOnlyChange(roles)` so search need not
rescan the entire tree on every age/value change.

Metadata access, `data()` and local `fetchMore()` never submit I/O. The shared
progress owner polls previews after foreground work, Watchlist and Batch. The
preview reader does not select a Details Field, replace its snapshot, edit its
draft, or invoke a Command or Service. Watch frames can warm a matching preview
cache without a duplicate read.

## Bounds and lifecycle

| Limit | Contract |
| --- | --- |
| Refresh age | 5,000 ms per Field; older values are visibly stale |
| Request spacing | At least 500 ms between attempts; at most two starts per second |
| Accepted preview requests | At most one pending; no timer backlog |
| Cached Fields | At most 64 |
| Total cached payload | At most 256 KiB, also subject to the Field count limit |
| One preview payload | At most 8 KiB; larger Fields say Open Details |
| View leases | At most 16 |
| Demand per lease | At most 256 IDs; this is not a global 256-ID union limit |
| Model expansion | At most 4,096 additional descendant rows per model |
| Expansion page | At most 128 rows per `fetchMore()` |
| Large arrays | Groups of at most 256 elements |

These are work and cache storage bounds, not a guaranteed sampling frequency or
the component's total RAM. One detached completion can additionally own up to
8 KiB; copied metadata, model labels and Qt/container overhead are separate. Preview
work yields to an occupied Session. A request already accepted before a panel is
hidden may drain; removing demand does not pretend that an accepted operation
vanished. Its completion cannot warm a replacement visibility lease or session.
`QueueFull`/busy refusals do not add accepted requests or mark values fresh.
Retirement and invalidation remove old values; late results cannot repopulate a
new admission. Cache or expansion limits remain explicit rather than silently
allocating more memory. Use Details for values beyond those limits.

## Shared Dynamic editor metadata

Field, Command and Service editors use the same `ValueModel` roles:

- `editorKind`: `bool`, `enum` or the text fallback.
- `editorChoices`: at most 256 `{label, text}` entries. Boolean labels are
  `false (0)` / `true (1)`, with canonical `false` / `true` text. Enum labels
  contain both the name and exact decimal code, including signed codes.
- `editorChoicesTruncated`: the descriptor has more than 256 named entries.
- `canonicalText`: the actual prepared typed value, separate from unfinished or
  invalid text in the GUI. It does not claim that a device applied the value.
- `typeText` and the compatible `type` role: short schema type descriptions.

An enum remains open: an unknown numeric code can be entered manually. U64/S64
codes and values stay strings and do not pass through a QML Number. F32/F64
canonical readouts use 9/17 significant digits for round trip; the GUI can retain
the entered `0.1` while showing the F32 encoded readout `0.100000001`. Overflow
and underflow are rejected by the existing typed conversion before submission.
No promise is made that zero initial arguments are device defaults. This slice
adds Dynamic provider metadata/editors; it does not claim matching Static
provider dropdown or background-preview support.

## Maintained tests

`tests/qt_connection/run.py` builds and runs all three targets against installed
SDK/UX packages, alongside its existing TCP/QML, scheduling, Batch, layout,
search, group Watchlist, Trend and example checks. It records source/package
hashes before and after the run, executable/log hashes, and UI captures.

- `telemetry_endpoint_previews_check`: controlled clock and completion slots,
  cache/demand bounds, refusal/payload errors, stale epochs, Watch warming,
  detached callbacks and reentrancy.
- `telemetry_endpoint_preview_models_check`: actual parsed descriptors and
  Encoded client operations, metadata without getters, local array offsets and
  expansion bounds, ID zero, stable indexes, deletion during publication,
  schema-only Details, draft-preserving reads, bool/signed enum choices and
  precise numeric conversion. It uses offscreen `QGuiApplication` because
  `QAbstractItemModelTester` also validates GUI role metatypes; it opens no window.
- `telemetry_endpoint_previews_ui_check`: the real reusable QML panel, visibility
  demand and narrow layouts, with strict warnings and captured output supplied
  as its first argument.

Before the tree-table follow-up, the complete installed-package runner passed
on Windows with Qt 6.12.0 and
MinGW 15.1, including **1,020 core checks**, **897 model checks** and **478 native
UI checks**, all with zero warnings. It also retained the existing two-panel
TCP, scheduling, Batch, layout, search, group Watchlist and Trend checks and built
the standalone connection example. The run preserved identical source and
installed-package hashes and recorded seven endpoint UI captures. The README's
result paragraph records that earlier preview foundation, not the current
tree-table/editor acceptance. The maintained runner writes current check
counts, source hashes and captures in its own `summary.json`.
These are local host results, separate from CI, STM32, DMA or physical-transport
qualification.
