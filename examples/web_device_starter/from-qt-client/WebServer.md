## Optional HTTP / WebSocket application adapter

Install the **Qt WebSockets** module matching the desktop kit and configure
`-DTELEMETRY_EMULATOR_WEB=ON`. Normal TCP-only builds do not require this module.
Prepare the browser assets with the sibling client tool, then run:

```text
telemetry-device-emulator --web-root C:/exports/device/www --http-port 8080
```

Open `http://127.0.0.1:8080`. The HTTP server accepts only exported, hashed assets
and the documented Descriptor/capability bootstrap. The SDK's existing
`demoWebSocket` reference profile uses a separate automatically selected local
WebSocket port. Actual framing/upgrade are handled by Qt WebSockets. HTTP and
WebSocket listeners bind localhost only; this is a trusted local development
adapter, not a remotely authenticated device server.

Web and TCP share the exact same native Model, workspace and Encoded server.
Only one can own a session; the emulator's Disconnect button closes either
carrier. Replies are copied before native storage is released. The adapter
accepts one complete binary TEQ1 message at a time, with 8192-byte frame/message
limits and no mutation replay. It is compatible with `--read-many-only`; legacy
Descriptor-only mode intentionally refuses a Web lease.

Assets are validated at startup (4 MiB/256 files, sizes, MIME, SHA-256, plain
paths, no links) and held in owned memory. This is not an MCU Flash/RAM estimate.
HTTP headers are limited to 8192 bytes, 16 clients and five-second deadlines;
only GET without a body is accepted. Foreign Host/Origin, traversal and unknown
files are rejected. There is no general filesystem browser or command endpoint.
