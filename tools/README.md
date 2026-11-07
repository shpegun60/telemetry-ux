# Browser deploy assets

For a new QML or browser application, start with
[`create_project.py`](../doc/ProjectStarter.md). It creates a complete editable
project from the maintained examples. Choose `qt-connection` for two ready QML
panels over the SDK-owned TCP reference session, `dynamic-qml` for a local native
Model, or `web-device` for the HTTP/WebSocket application. The exporter below packages an application
that has already been prepared; it does not create a project or connection.

`export_web_assets.py` turns a **prepared public directory** into an explicit,
relocatable set of browser files. Run npm installation and any application SDK
generation on the development machine first. Python and Node are not needed on
the device that serves the result.

For the prepared `examples/web_demo` output:

```powershell
python tools/export_web_assets.py `
  --public-root C:/build/telemetry/public `
  --output C:/build/telemetry/firmware-www `
  --asset index.html --asset app.js `
  --asset generated/js/device.js `
  --asset generated/js/device.metadata.js `
  --asset generated/ui/DevicePage.js `
  --cpp
```

The output directory must not exist. Nothing is deleted or overwritten. Choose
application assets explicitly; tests, declarations, generators, package-manager
metadata and unrelated source files are not copied. A dynamic-only application
can omit the generated files if its own app does not import them.

The tool copies the supported installed `telemetry-sdk` and `telemetry-ux` runtime
module closure and licenses under `vendor/`. It preserves the SDK's relative
`web/` to `lib/client/web/` imports, and rewrites the supported import-map entries
and Configurator stylesheet URL in `index.html`. There is no CDN dependency or
`node_modules` directory in the exported runtime. The input import map must use
the documented installed-package paths from `examples/web_demo/index.html`.
This is a deploy copier for that ESM layout, not a JavaScript bundler.

The prepared [Session demo](../examples/session_web/README.md) is also supported.
Select `index.html`, `SessionDemo.js`, `generated/js/device.js` and
`generated/js/device.metadata.js` as application assets. Its explicit
`telemetry-sdk` root import-map entry opts into the fixed SDK `web/index.js`,
`web/Session.js` and `web/WebSocketConnection.js` files. Without that root entry,
the existing client/carrier export closure stays unchanged and includes none of
those Session files. The same preflight, byte/file limits and reference checks
apply before any output is created. Session export also supplies the fixed local
SDK carrier import-map entry needed by the preserved UX carrier wrapper.

`assets.json` contains sorted URL paths, MIME types, lengths, SHA-256 values and
installed package metadata identities. It has no build time or absolute source
path. `--cpp` additionally emits `telemetry_web_assets.hpp`: C++17 immutable byte
arrays, an asset table, and a small `find(std::string_view)` lookup. Empty assets
have logical length zero. The manifest/header are deploy artifacts; serve the
manifest's asset paths, not arbitrary files from the build directory.

The optional C++ table does not allocate, copy a response, create a task, queue
or protocol. Its full byte size counts toward the firmware's Flash budget. A
project can instead serve the exported files from its own filesystem. Default
limits are 256 files and 4 MiB of runtime data; use `--max-files` and `--max-bytes`
for a smaller firmware budget. Header text expansion is a build artifact and is
not included in that runtime-byte limit.

All input paths and file budgets are checked before creating output. Reference
checks cover ordinary literal module imports/re-exports, HTML `src`/stylesheet
links/`srcset`, CSS `url(...)` and quoted `@import`, including inline style blocks
and attributes. Nonliteral CSS imports are refused. Symlinks/reparse points,
traversal, case aliases, remote
asset URLs, missing dependencies and existing destinations are refused. Keep
the input tree and output parent trusted and unchanged during export; this is
not a defense against another process replacing paths between checks and writes.
A disk error can leave an incomplete **new** directory; inspect the successful
exit and manifest before deploying it. No existing output is reused.

The reference scanner is not a general JavaScript/CSS parser or security filter.
The exporter does not interpret arbitrary application JavaScript, computed
imports, runtime `fetch()` URLs or authentication logic. Review those in the
application. It supplies no HTTP/WebSocket implementation, TLS, authentication,
authorization or session admission. Browser write opt-in is local consent, not
server authorization. A firmware WebSocket stack must assemble bounded binary
messages, then pass them to the existing SDK Encoded dispatcher with stable
caller-owned buffers. Disconnect must invalidate admission and finish pending
work/TX ownership before reuse; it does not undo a possibly executed mutation.
Mixed Batch authorization must cover every item before any item executes.

The existing `examples/web_demo/serve_ws.py` is a localhost reference using a real
native SDK dispatcher. Exporting its browser app does not turn its `/demo/*`
bootstrap routes into a firmware API. An actual device must provide an explicitly
chosen bootstrap/session policy and Descriptor/capability routes. The NUCLEO UART
example is not an Ethernet or HTTP server. No Ethernet hardware claim follows
from an export, host browser test or ARM compile.

Qualification from an already prepared public tree:

```powershell
python tests/web_assets/run.py --public-root C:/build/telemetry/public `
  --build-dir C:/build/telemetry/assets-check `
  --cxx C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  --arm-cxx C:/path/to/arm-none-eabi-g++.exe `
  --browser-python C:/build/browser-venv/Scripts/python.exe `
  --device C:/build/native/telemetry_web_demo_peer.exe
```

Add `--session-demo` when the prepared directory contains the Session demo.
Its relocated browser journey exercises two independent views, exact integer
editing, stale sibling drafts, historical outcomes and explicit reconnect over
the existing native WebSocket server. The default runner retains the original
client/carrier demonstration and its existing closure.

The runner checks refusal paths, two deterministic relocated exports from actual
installed packages, every manifest hash, and unchanged input hashes. Optional
host compilation executes table lookups; optional Cortex-M7 compilation checks
the generated header without claiming firmware linking, Flash size or physical
network operation. Supplying both browser arguments runs the real Static and
Dynamic browser journeys against the relocated files and the native dispatcher:
read/edit/write/readback, mixed Batch, late replies and reconnect, with strict
browser diagnostics. The browser environment needs the pinned Playwright and
websockets dependencies used by the existing WebSocket gate. CI runs this
deployed-assets journey and retains its manifest and receipts.
