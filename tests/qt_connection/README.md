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

`--sanitize` verifies ASan/UBSan instrumentation in six installed archives and
uses the existing narrowly named Qt software texture leak exception. A separate
123-byte injected application leak must still fail with exit 86. The runner
builds the standalone example against installed packages, guards source and
installed-input hashes, and writes a receipt. No physical device is touched.
