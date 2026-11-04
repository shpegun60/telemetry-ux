# Browser Configurator with a native demo device

This application selects either a generated static SDK or a runtime Descriptor
client, then mounts the same browser Configurator. The host owns its one client,
admission and connection. The view and generated page own presentation only.

The native helper keeps one actual Telemetry Model alive between requests.
Configuration writes clamp `gain` to 10, so Apply reads back a value that can
differ from the draft. It also supplies exact U64/S64, signed zero, open enum,
nested arrays, read-only Fields, Command Accepted/Executed, Service refusal,
Void and empty-structure responses.

The diagnostics catalog also has scalar `Wide`, `Signed`, `Mode` and
`Unavailable` Fields for the Watchlist. Add them and press Refresh to issue one
ReadMany request with per-item statuses. Individual FieldRead fallback is an
explicit option. Start enables periodic reads; Stop drains an accepted request
and stops future ticks. The application owns the 50-ms tick timer and discovers
the session-bound TRM1 bytes through its local bootstrap route on each connect.
The provider creates no timer, client or FIFO.

Prepare an installed SDK 0.1.0 prefix containing `core` and `encoded_server`,
matching SDK/UX npm tarballs, and a generator environment containing both matching
`telemetry-codegen-core` and `telemetry-ux-codegen` distributions. From this repo:

```sh
cmake -S examples/web_demo/native -B /external/native-demo -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/installed-sdk
cmake --build /external/native-demo --parallel 2
mkdir -p /external/browser-demo
npm install --prefix /external/browser-demo --ignore-scripts --no-audit --no-fund \
  /path/to/telemetry-sdk-0.1.0.tgz /path/to/telemetry-ux-0.1.0.tgz
/external/native-demo/telemetry_web_demo_peer --descriptor /external/descriptor.bin
/external/codegen-env/bin/telemetry-codegen generate /external/descriptor.bin --targets js \
  --structural-metadata --ux-starter web --output /external/browser-demo/generated
cp examples/web_demo/app.js examples/web_demo/index.html /external/browser-demo/
python3 examples/web_demo/serve.py --device /external/native-demo/telemetry_web_demo_peer \
  --public-root /external/browser-demo --data-dir /external/demo-state --port 8080
```

Open `http://127.0.0.1:8080`, choose a client mode and click **Connect demo**.
Select a Field and press **Read**. To write, enable changes, press **Edit**, change
text and click **Apply + readback**. Commands and Services create their request
forms automatically. Their initial zeros are local editor values, not device
defaults. The standalone [native helper](native/README.md) and
[installed consumer gate](../../tests/web_configurator/README.md) describe the
same data and execution boundaries.

The HTTP adapter forwards complete TEQ1 messages to the SDK dispatcher and
performs no Telemetry encoding, scheduling or retries. It listens on localhost
and supports one demo session at a time. Static mode obtains only the observed
64-byte header; dynamic mode downloads and parses the Descriptor through the SDK.
The example's routes are an application transport, not a firmware HTTP server.
No physical device, Ethernet, UART or DMA is involved.

For the persistent WebSocket demonstration, use the same prepared native helper
and browser directory. Install `websockets==15.0.1` in an application-owned Python
environment and replace the final server command with:

```sh
/external/host-env/bin/python examples/web_demo/serve_ws.py \
  --device /external/native-demo/telemetry_web_demo_peer \
  --public-root /external/browser-demo --data-dir /external/demo-ws-state --port 8080
```

The HTTP listener serves the same files and header/Descriptor. Transport
discovery selects its separate localhost WebSocket listener; the host receives
one session greeting, then sends complete binary TEQ1 messages through the
ordinary SDK `EncodedBackend`. There is no HTTP fallback, retry or automatic
reconnect. The server grants one lease and does not reuse the native peer while
an old exchange still owns it. This example exposes operations 1-5, including
ReadMany; Batch remains a separate follow-up. The [installed WebSocket gate](../../tests/websocket_transport/README.md)
keeps actual browser/native journeys separate from carrier and lease fixtures.

**Hide view** removes only the DOM/subscription; **Show view** reuses the same
provider and client without issuing a request. **Disconnect** retires displayed
values before closing the client. A pending mutation's checked outcome can still
finish and remains visible; reconnect starts a new presentation owner and does
not replay the old operation. The host controls actual transport quiescence for
its real device connection.

For WebSocket, Disconnect also closes the host-owned socket, including during
session bootstrap or after admission became invalid. A failed Connect cleans its
candidate socket even if no SDK client was created. The carrier only detaches
its own listeners; socket closure is not automatic proof of remote quiescence.

For your own application, import the generated `mountDevice` entry page, supply
your admitted client and use your chosen transport. Keep user code outside the
generator's owned output directory. Browser watchlists and batch actions are
separate follow-up features; this example exposes Field, Command and Service.
