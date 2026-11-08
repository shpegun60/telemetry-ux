# Installed own-device starter journey

Run the exact shipped preparation command from the [starter README](../../examples/web_device_starter/README.md),
then use a Python environment with `websockets==15.0.1`, Playwright and its Chromium:

```sh
python -B tests/web_device_starter/run.py --prepared /external/my-device \
  --output /external/my-device-browser-check --exported
```

The output must be new. This test starts the existing reference HTTP/WebSocket
server with the starter's own compiled Model and uses the shipped browser page.
It checks discovery, exact U64, real Read → Edit → Write → normalized readback,
Command, Service, lost acknowledgement, uncertainty, explicit reconnect, no replay,
ReadMany Watchlist, a four-item mixed Batch, stale-value suppression and terminal close.
With `--exported`, the runner verifies and relocates `www/`, then serves only those
runtime files. It checks missing browser assets and keeps the copied bundle immutable.
The default Dynamic preparation needs no generated modules, and forcing unavailable
Static mode must fail before any bootstrap, socket or fallback. Static is exercised as well when the
prepared application contains generated files. It records browser diagnostics,
source/executable hashes and screenshots in `summary.json` and sibling files.
Native process exit and stderr are checked after shutdown, including sanitized builds.

Verify a prepared deploy manifest and every C++17 table byte independently:

```sh
python -B tests/web_assets/run.py --export-root /external/my-device/www \
  --build-dir /external/my-device-table-check --cxx g++
```

The table check compares every path, MIME type, length and byte with the manifest
and relocated files. A `files` export has no C++ header and receives the same
manifest/relocation checks without compilation.

This is host HTTP/WebSocket qualification. It does not prove STM32 HTTP, Ethernet,
UART, DMA, access control or firmware Resource discovery behavior.
