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

Use **Watchlist and Trend** to open the second provider's watch controls and a
single-Field chart. Add a scalar Field to Watchlist, choose it in Trend, and
explicitly start Watchlist polling or press Read once. Trend does not issue any
requests or own a client. `TrendHistory` is a separate QObject per chart; the
host provides it to QML along with the existing provider. The chart stores at
most 512 readings and uses the host's monotonic presentation time, not a device
timestamp. It leaves gaps after stale/error values and changed sessions. Large
integers that cannot be represented exactly as chart numbers are not plotted;
their original text remains available in Watchlist. This is a host polling view,
not an acquisition scope or a guarantee of sampling rate.
Construct and access `TrendHistory` on the Qt owner thread; the Watchlist model
and history must belong to that same thread. A source from another thread is
refused. Keep the history alive until the QML panel has been destroyed.

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

For labels, units and help, read your optional
[presentation document](../../doc/PresentationMetadata.md) into a `QByteArray`
and call `presentation.setPresentation(json)` once. Existing and future providers
receive the same hints, while keeping independent selection and drafts. Check
its boolean result and `presentation.presentationError()` before reporting the
configuration as loaded. Empty input clears it. Range and step hints are
advisory, and no hint grants write permission or submits a request.

The example also accepts `--presentation path/to/presentation.json` (set it in
Qt Creator's run arguments). The file is validated before any connection opens;
missing or invalid input is reported without starting a session.
