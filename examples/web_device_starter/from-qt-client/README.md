# Reference for slice W6: not built, not shipped

The browser page of the Telemetry Qt client's former workspace, kept here as the
starting point for slice W6. Its folder there was retired on 11 November 2026.

- `DeviceApp.js`, `index.html`, `favicon.svg`: a Dynamic-only variant of this
  starter's page, with careful handling of connection attempts and their
  retirement (`attempt`, `pendingTask`, `retirement`, `epoch`, `disposed`).
- `prepare_web.py`: how it prepared its assets from local SDK and UX packages.
- `check_web.py`: its headless browser check.
- `WebServer.cpp`, `WebServer.hpp`, `WebServer.md`: the standalone emulator's
  HTTP and WebSocket adapter that served the page. It retires: this starter's
  `native/` device serves the same contract.

**W6:**
1. Fold the attempt and retirement handling into this starter's `DeviceApp.js`, or
   into the SDK's `Session` if it turns out to be lifecycle logic.
2. Make the starter Dynamic only, together with slice B3.
3. Move what `check_web.py` covers beyond `tests/web_device_starter` into that test.
4. Delete this folder.
