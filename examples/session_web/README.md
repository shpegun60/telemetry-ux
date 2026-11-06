# Two independent views over one connection Session

This example uses the existing host [WebSocket demo server](../web_demo/README.md)
and its native application Model. It adds no new server, protocol or queue.
Both Configurators share one Session while owning separate selection and drafts.

Prepare that demo's installed SDK/native helper and generated JavaScript context,
then install the matching SDK/UX npm tarballs into an external public directory.
Use the package pair that includes Session-preserving writes and the Session UX
adapter; the development `0.1.0` label alone does not identify the source revision.

```sh
npm install --prefix /external/session-demo --offline --ignore-scripts \
  /path/to/telemetry-sdk-0.1.0.tgz /path/to/telemetry-ux-0.1.0.tgz
# Generate /external/session-demo/generated with the existing native Descriptor:
telemetry-codegen generate /external/descriptor.bin --targets js \
  --structural-metadata --wire-editing --output /external/session-demo/generated
cp examples/session_web/index.html examples/session_web/SessionDemo.js /external/session-demo/
/external/host-env/bin/python -m pip install websockets==15.0.1
/external/host-env/bin/python examples/web_demo/serve_ws.py --device /external/native-demo/telemetry_web_demo_peer \
  --public-root /external/session-demo --data-dir /external/session-state --port 8080
```

Open `http://127.0.0.1:8080`. Select Dynamic or Static and connect. Choose different
endpoints in the two views; reads and mutations use the same FIFO. A write in one
view marks the other view's data stale. Disconnect leaves historical operation
outcomes visible. Reconnect is explicit and never replays a mutation. Close ends
that Session; a new connection uses the same profile and requires a fresh lease.

Use the Python environment prepared in the linked demo, not an unrelated system
interpreter. On Windows its executable is `/external/host-env/Scripts/python.exe`;
use Windows paths and the native helper's `.exe` suffix accordingly.

The server checks browser Origin, so load this page from its HTTP origin. It is a
localhost reference application, not STM32 firmware or a production authentication
implementation. No board or Ethernet/DMA qualification is implied.

[Session provider contract](../../doc/SessionProvider.md)
