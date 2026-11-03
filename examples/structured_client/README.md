# Structured payload tools

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This example opens Descriptor and ValuesFile bytes, creates a Service request
form, saves canonical request payloads and decodes response files. An optional
localhost demonstration calls a native C++ application fixture. For the normal
Field/Command/Service Configurator workflow, use [web_demo](../web_demo/README.md).

The UI and HTTP adapter belong to Telemetry UX. The browser imports the installed
`telemetry-sdk/runtime`; the native helper links installed `Telemetry::core` and
`Telemetry::resource_adapter`. No SDK source checkout or generator is required.

## Run the example

Start from the Telemetry UX checkout. Obtain an installed SDK 0.1.0 prefix with
`core` and `resource_adapter`, and the matching `telemetry-sdk-0.1.0.tgz` npm
archive. These development packages are explicit inputs, not registry releases.
The following POSIX commands keep all generated files outside the checkout:

```sh
cmake -S examples/structured_client -B /tmp/telemetry-payload/native \
  -DTelemetry_DIR=/path/to/sdk-prefix/lib/cmake/Telemetry -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/telemetry-payload/native --parallel 2
mkdir -p /tmp/telemetry-payload/runtime
npm install --prefix /tmp/telemetry-payload/runtime --offline --ignore-scripts \
  --no-audit --no-fund /path/to/telemetry-sdk-0.1.0.tgz
python3 examples/structured_client/serve.py \
  --device /tmp/telemetry-payload/native/telemetry_structured_device \
  --data-dir /tmp/telemetry-payload/data \
  --sdk-package-root /tmp/telemetry-payload/runtime/node_modules/telemetry-sdk --port 8080
```

Open `http://127.0.0.1:8080/` and press **Load fake Device**. On Windows the helper
has an `.exe` suffix; place the selected MinGW compiler's runtime DLL directory
first on `PATH` when launching it. The server binds to localhost and serves only
the known UI assets, one installed runtime asset, Descriptor/ValuesFile bytes
and the example Service route. It does not expose source directories or arbitrary
`node_modules` files.
An existing application's installed `node_modules/telemetry-sdk` also works;
placing npm dependencies outside the checkout is optional for this server.

The page can instead open a descriptor file, save a request, open a response
payload and decode a matching values file. Loading a descriptor from disk disables
**Call fake Device**. U64/S64 inputs remain decimal text converted directly to
BigInt. Unknown representable enum codes are accepted; names are suggestions.
The request form starts at local UI zeros, including enum code zero. These are
not device defaults. The form expands at most 4096 nodes and file uploads are
capped at 16 MiB.

## Demonstration contract

[Device.hpp](Device.hpp) contains the application DTOs, reflected declarations
and fake methods. [Device.cpp](Device.cpp) exports the example bytes and invokes
an encoded endpoint through public installed SDK APIs. Their provenance is SDK
commit `6e15220b1271139b3f1996a16d385c660f856f45`, previously
`examples/structured_client/Device.hpp` and `tests/structured/client/Device.cpp`.
No SDK runtime, parser or codec implementation is copied into UX.

One `Config` type is shared by a Field, Command and Service. The Service
`multiplier` values 0, 1, 2 and 3 deliberately return InvalidArgument, Busy,
Unavailable and Failed; these are application decisions, not descriptor limits.
Void and an unavailable FunctionSlot are separate examples.

The illustrative HTTP adapter uses `POST /service/<u32-id>`, a raw payload and
`X-Dispatch-Status` / `X-Endpoint-Status` headers. The page permits one outstanding
call and never repeats a timed-out operation. A timeout can occur after native
execution; the page reports an unknown outcome. Selection or descriptor changes
retire pending display and delayed file-read results.

Each HTTP call starts a **fresh native process**. Its application state does not
persist between requests. This is a payload-tool demonstration, not a firmware
HTTP server or a replacement for the SDK DeviceClient lifecycle. It creates no
additional SDK client or FIFO.

The maintained browser qualification is documented in
[tests/structured_client](../../tests/structured_client/README.md). Running that
qualification is not a prerequisite for starting this example.
