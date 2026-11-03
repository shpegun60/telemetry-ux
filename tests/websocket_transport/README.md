# Installed WebSocket carrier and browser journeys

This offline gate builds the existing application-owned native Model against an
installed SDK 0.1.0 prefix. It generates a static SDK with installed core/UX Python
packages, packs/installs the real npm packages outside both repositories, and
uses Chromium against the localhost HTTP/WebSocket host. No SDK source fallback,
device, firmware, UART or DMA is involved.

```sh
python3 -B tests/websocket_transport/run.py \
  --build-dir /external/websocket-gcc \
  --sdk-prefix /external/installed-sdk \
  --sdk-package /external/telemetry-sdk-0.1.0.tgz \
  --codegen-python /external/codegen-venv/bin/python \
  --browser-python /external/browser-venv/bin/python \
  --node /external/node/bin/node --tsc /external/typescript/lib/tsc.js
```

The browser environment needs Playwright 1.63.0 with its Chromium and
`websockets==15.0.1`. For the instrumented profile add `--sanitize --cxx clang++-18`
and supply a fully ASan/UBSan-instrumented SDK prefix. The runner inspects the
actual installed native archives. It requires fresh output directories and
preserves logs, before/after source/package/tool identities, executed native
binary identity, screenshots and separate results for each scope.

- `Port.mjs` is an explicitly fake-open-socket fixture for bounded sends,
  listener ownership and ordinary backend integration. It is not a second FIFO.
- `server_test.py` isolates the real host lease/admission code with controlled
  native I/O and real WebSocket clients. It does not claim native Model execution.
- The maintained HTTP browser journeys run unchanged, preserving the original
  transport behavior and renderer checks.
- `browser.py` uses the actual persistent native dispatcher in Static and Dynamic
  modes. It checks exact integers, real normalization/readback, Command
  Accepted/Executed, Service refusal/response, hide/show without I/O, lost-reply
  uncertainty and explicit reconnect without replay. Greeting faults are labeled
  test-only host transport hooks and prove candidate cleanup before admission.

The carrier never owns a client, bootstrap, execution queue or remote closure
assertion. The application owns Connect/Disconnect and the borrowed socket.

Current maintained floors are 241 isolated carrier checks, 79 host ownership
checks, and 70 real WebSocket browser checks across the two client modes.
The unchanged HTTP browser gate also runs its own existing journey/renderer
assertions. Every individual assertion remains authoritative.

A native pipe timeout or incomplete I/O permanently revokes demo admission:
local socket closure cannot prove that old native execution stopped or that its
reply stream is synchronized. Restart the demonstration after such a host error.
The regression uses an explicitly controlled pipe; it does not call this a
physical-device or native long-running-endpoint test.
