# Qt/QML runtime Inspector checks

This suite builds the [runtime Inspector example](../../apps/inspector/README.md)
against Qt 6.4 or newer. It uses the actual `EndpointModel`, `ValueModel`,
`InspectorController` and native Encoded demo peer, without generated SDK files.

The maintained Watchlist executable exercises 32 bounded rows, owned exact
scalar/enum text, real ReadMany dispatch, explicit individual-read fallback,
missed polling ticks, stop/drain, immediate admission invalidation and actual
QML mouse/keyboard controls. The runner saves a separate Watchlist screenshot.

`WatchlistFailures.cpp` uses a real loopback TCP peer and the shipped QML
delegates. It checks that foreground FieldRead, Command, Service, Apply Write
and Apply readback failures clear already-rendered values and publish stale
status, stop polling and complete once without replay. Queued and handed-off
Watchlist reads retain their pending state until the actual callback drains.
Both drain phases explicitly refresh the invalid DisplayRole of a live endpoint
delegate before the owner resets its tree. Every QML warning remains a test
failure, with the queued or handed-off phase included in its diagnostics.
`WatchlistScheduling.cpp` pauses Individual cycles behind foreground reads and
checks that list/mode/frequency changes and new cycles cannot disturb the
remaining Field ID order. Stop remains available. Actual QML buttons reject
Read once, Clear and Remove between polling ticks.

```sh
python3 -B tests/inspector_qml/run.py --build-dir /external/inspector-qml \
  --sdk-prefix /external/sdk-prefix --qt-prefix /usr --cxx /usr/bin/g++ --cmake /usr/bin/cmake --ninja /usr/bin/ninja
```


The runner requires an installed SDK prefix explicitly through `--sdk-prefix`.
It passes that package's exact `Telemetry_DIR` to CMake and never reads an SDK
source tree. `summary.json` records SDKMetadata (version, source revision and
wire versions) separately from the UX revision, and fingerprints all files in
the actual installed prefix before and after execution. SDK or UX input drift
fails qualification. The prefix and fresh output directory must be disjoint.

On Windows select the Qt kit's matching MinGW compiler, Qt prefix, native CMake
and Ninja explicitly. The runner places the selected compiler and Qt runtime
before other toolchains in `PATH`. Every build, log and `summary.json` stays in
a fresh directory outside the repository. Windows offscreen runs explicitly
use the system `Windows/Fonts` directory so the PNG contains readable text.
A missing example or failed/expired command fails the run. Source inventories
and raw SHA-256 digests must remain
unchanged from start to finish.

`QAbstractItemModelTester` checks both models, with explicit lazy-fetch tests
for the 16,384-element array. `QSignalSpy` observes resets, insertions and read
completion. The maintained floor is 763 executed checks and 23 real FieldReads.
Real Encoded reads check exact U64/S64 decimal strings, an unknown
signed enum, nested structs/arrays, an empty struct, owned snapshot storage and
stale model indexes. Controller checks cover metadata-only files, selection
without I/O, mutation opt-in and Service refusal, accepted refresh invalidation, QueueFull
preservation, delayed old replies, fresh sessions, timeout/recovery and shutdown.
Direct slots on reset and row-insertion notifications attempt reconnect and
disconnect; both must preserve the active model mutation and current session.
Reactive loaded-count notification is checked under the same guard.
All endpoint metadata roles reject schema or admission invalidation immediately,
before any controller pump or Qt model reset; tree topology remains unchanged
until its explicit reset.

The second executable creates the actual QML window offscreen and performs a
FieldRead. Its success marker requires both a root window and a published
snapshot, visible endpoint delegates and four visible value rows including the
decoded `Ua` child. The runner retains those view counts in its summary.
These are host Qt and protocol checks; they do not establish device,
display-server or physical transport behavior.

Workbench scenarios exercise exact text input for U64/S64 and enums, nested
leaves, invalid-input atomicity and Apply gating, device-side normalization,
read-only profile refusal and unchanged default Inspector behavior. A separate
`--edit-smoke` process invokes the actual QML Edit button, TextField handler and
Apply button. Its success requires one Write and normalized Gain=10 readback.

Explicit Disconnect tests choose queued, offered, prepared, executed and
confirmed-Write phases directly, with exactly one terminal signal and the
appropriate Cancelled/OutcomeUnknown/known-Write result. Reentrant disconnect
from synchronous state notifications cannot suppress that completion.

Session-history checks retain confirmed, Cancelled and OutcomeUnknown outcomes
with their original session ID. Repeated reconnects cannot nest historical
labels, a refused Apply preserves history, and a new accepted Apply shows its
current Pending/result without an old-session label or duplicate completion.

`QmlInput.cpp` loads the shipped Main.qml in a real QQuickWindow and sends mouse
and keyboard events. It types Gain=12 and clicks Apply without Enter, then edits
Gain=4.25 and an exact U64 Counter and clicks Apply while the Counter retains
focus. Both writes and their readbacks must finish once. This test does not set
the editor text property or invoke editingFinished/clicked signals directly.

The runner retains both read-only and edited window PNGs and their dimensions/hashes.
`Command.cpp` uses the same QML argument controls and real keyboard/mouse events
to Execute without Enter. It separately checks Executed, Accepted and unknown
outcomes, no-argument commands, shared exact scalar editing, one terminal signal
on Disconnect and preservation of historical outcomes. These requests use the
same Workbench/client as Fields; UART mutations stay disabled.
`Service.cpp` uses actual keyboard/mouse events to Call without Enter, reads the
owned Response through the shared lazy ValueModel, and distinguishes success,
InvalidArgument and OutcomeUnknown. Request and Response are presented separately;
their endpoint/session context and one terminal signal survive Disconnect.
The Service window PNG is retained alongside Field and Command screenshots.
On Linux, `--sanitize --cxx clang++-18` enables fatal AddressSanitizer and
UndefinedBehaviorSanitizer checks. Leak detection is explicitly disabled for
the Qt offscreen plugin's process-global allocations; this profile does not
claim LeakSanitizer coverage.


The reusable `Telemetry.UX.Configurator` is also loaded inside `Embedded.qml`,
an independent host window that passes its existing provider as an initial
property. No `controller` context property is set. Actual mouse/keyboard input
performs Read, Edit and one Apply/Refresh with device-normalized Gain=10. The host
uses a light surface; a pixel in the view's layout gap must retain its own dark
palette background so pale outcome/status text remains readable.
The host
can unload its view after disconnect while the same engine and provider remain
alive; queued layout work must not warn or touch destroyed tree items. Valid
embedding runs retain strict QML-warning checks. The missing required-provider
case runs in a separate negative process: Qt may emit constructor binding
diagnostics before rejecting the missing dependency. The exact required-property
creation error is checked, and the complete negative log is retained.
