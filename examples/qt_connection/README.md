# Owned Qt TCP connection

Open this CMake project in Qt Creator with installed SDK and UX prefixes in
`CMAKE_PREFIX_PATH`. The SDK must include `qt_connection`; build UX with
`TELEMETRY_UX_BUILD_QT_CONNECTION=ON`. Link `TelemetryUX::qt_connection`.

`main.cpp` creates one `ConnectionSession`, one presentation adapter and two
independent providers. No application-owned pump, Descriptor indexing, transport
copy or generated DTO is needed. The session progresses on its Qt owner thread.
Both Configurators include their own endpoint tree. After connecting, choose a
Field independently in each panel and press Refresh. Neither panel requires a
hard-coded endpoint or a programmatic selection before it can be used. Selecting
an endpoint alone sends no request; refreshing one panel does not fill the other.

Connect to the maintained Inspector TCP reference server (default localhost:5577).
This is the exclusive Resource-bootstrap/Encoded profile, not arbitrary TCP.
Read-only is the initial UI policy; the example host allows explicit per-panel
mutation opt-in. Disconnect preserves terminal mutation facts; reconnect is
explicit and never replays work. The same peer's complete fresh bootstrap supplies
remote supersession under this named server contract. Socket closure alone does
not prove remote recovery. UART/mutation recovery and generic connection profiles
are not provided by this example.

Destroy panels/adapter freely; the connection retains accepted work until drain.
Destroy the connection outside a native callback stack (use `deleteLater()` from
custom native callbacks). QML/provider notifications are delivered after native
processing, so ordinary Qt UI callbacks can disconnect or remove panels safely.
