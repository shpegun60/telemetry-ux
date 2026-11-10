# Installed native Qt connection qualification

Run `run.py` with external `--sdk-prefix`, `--ux-prefix`, `--qt-prefix` and a fresh
`--build-dir`. Both prefixes must have their optional `qt_connection` components.
The fixture imports the installed Session/carrier/presentation archives. Only the
maintained demonstration server/Model are compiled from the application; no
InspectorController, copied session driver or private TCP client is compiled.

Two actual QML panels use one Session/FIFO. Tests cover exact U64, independent
providers, sibling invalidation across adapters, FieldWrite/readback, queued and
offered cancellation, prepared/executed uncertainty, one terminal fact, no replay,
same-peer fresh admission, removing accepted panels, factory self-deletion,
reentrant disconnect and deleting Session from model reset. The same shipped
example QML is loaded with zero tolerated warnings.

The same gate also executes `tests/dynamic_watchlist`: controlled ticks and
completion slots check 65-row chunking, smaller capability budgets, explicit
Individual mode, Stop/drain, stale-source replacement and reentrant model
publication. Native callbacks fill owned slots without model notifications.

`tests/dynamic_batch` adds 239 controlled presentation checks for ordered
duplicate IDs, owned captured request bytes, exact item/request/reply limits,
mutation permission, QueueFull preservation, checked per-item facts, historical
uncertainty, and model-reset/fetchMore reentrant invalidation. Its submission
source is controlled; it is not a claim of native Batch execution. These QtCore
controls run without the Qt Quick texture suppression under sanitizers.

Presentation metadata is also checked over the actual TCP session: two existing
panels and a later panel inherit one adapter's immutable overlay without I/O.
Malformed replacement preserves prior hints; closing and replacing a panel
retains the configured overlay. Disconnect clears values, and a different
Descriptor reusing Field ID 0 does not inherit the old label or unit. Returning
to the matching schema restores hints without automatic reads or mutations.

`--sanitize` verifies ASan/UBSan instrumentation in six installed archives and
uses the existing narrowly named Qt software texture leak exception. A separate
123-byte injected application leak must still fail with exit 86. The runner
builds the standalone example against installed packages, guards source and
installed-input hashes, and writes a receipt. No physical device is touched.
