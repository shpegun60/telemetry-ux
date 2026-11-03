# Qt Quick generated API example

This UX-owned example runs the generated `DeviceApi` over native Encoded dispatch
(default) or Resource/RPC (`--rpc`). Both modes use its own in-process
[demo model](fixture/DemoModel.hpp); no physical device is connected. The DTOs,
endpoint names and initial values originate from the SDK codegen example at
`6e15220b1271139b3f1996a16d385c660f856f45`. Only application declarations and the
fixture exporter were copied. SDK codecs, dispatchers, clients and wire parsers
are consumed from an installed package.

Prerequisites:

- A matching-compiler Telemetry SDK 0.1.0 install containing `core`,
  `encoded_server`, `encoded_backend`, `rpc_backend` and `rpc_sessions`.
- Matching installed `telemetry-codegen-core` and `telemetry-ux-codegen` 0.1.0
  wheels in the chosen Python environment. These are development packages; use
  locally built wheels as described in the SDK
  [package guide](https://github.com/shpegun60/telemetry-sdk/blob/main/doc/SDKPackages.md).
- Qt 6.4+ Core, Gui, Qml, Quick and QuickControls2, CMake and Ninja. The Qt Quick
  Templates runtime module is also required. No SerialPort, COBS checkout or SDK
  source checkout is required.

From the UX repository root, use four steps: export the model, generate its SDK,
build the host, then run it. These six commands do not invoke a test runner:

```sh
sdk=/path/to/installed-telemetry
codegen=/path/to/codegen-env/bin/telemetry-codegen
out=/tmp/telemetry-generated-qml

cmake -S examples/codegen_qml/fixture -B "$out/fixture" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$sdk"
cmake --build "$out/fixture" --target telemetry_qml_fixture_export
"$codegen" generate "$out/fixture/native/descriptor.bin" --targets cpp,qml --output "$out/generated"
cmake -S examples/codegen_qml -B "$out/app" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$sdk" -DGENERATED_SDK="$out/generated"
cmake --build "$out/app"
"$out/app/telemetry_qml_example" "$out/fixture/native/descriptor.bin" "$out/fixture/native/config.bin"
```

The fixture build itself needs only installed `Telemetry::core`, without Qt.
Its explicit `telemetry_qml_fixture_export` target creates the descriptor and
canonical payloads in `fixture/native`; override `TELEMETRY_QML_FIXTURE_OUTPUT`
at configure time to choose another output directory. The producer never runs
during configure or a normal application build.

For Windows, use the compiler belonging to the selected Qt kit and put it before
other MinGW installations on `PATH`. For example:

```powershell
$sdk = 'C:/packages/telemetry'
$qt = 'C:/Qt/6.10.1/mingw_64'
$compiler = 'C:/Qt/Tools/mingw1310_64/bin/g++.exe'
$codegen = 'C:/venvs/telemetry/Scripts/telemetry-codegen.exe'
$out = 'C:/temp/telemetry-generated-qml'
$env:PATH = (Split-Path $compiler) + ';' + $qt + '/bin;' + $env:PATH

cmake -S examples/codegen_qml/fixture -B "$out/fixture" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$sdk" "-DCMAKE_CXX_COMPILER=$compiler"
cmake --build "$out/fixture" --target telemetry_qml_fixture_export
& $codegen generate "$out/fixture/native/descriptor.bin" --targets cpp,qml --output "$out/generated"
cmake -S examples/codegen_qml -B "$out/app" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$sdk;$qt" "-DCMAKE_CXX_COMPILER=$compiler" "-DGENERATED_SDK=$out/generated"
cmake --build "$out/app"
& "$out/app/telemetry_qml_example.exe" "$out/fixture/native/descriptor.bin" "$out/fixture/native/config.bin"
```

Append `--rpc` to run the Resource/RPC mode. Add `--smoke` in either mode to exit
after the first successful read. Offscreen automation may set
`QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software`. The app verifies its
full descriptor against the compiled demo model and decodes `config.bin` before
admission; the generated API admits only the exact 64-byte schema header.

The UI exercises Field read/write, Command execution, and a Service response.
Inspect returns a checksum over the configuration samples. Separate buttons
show Command `Accepted` and a Service refusal; acceptance is not physical work
completion.

QML creates `DeviceApi`; the C++ host explicitly attaches its input engine,
connects queued `requestReady` handling, and admits the exact generated header
before refreshing. Header admission declares schema identity; it is neither
authentication nor verification of a descriptor body. Each request carries its
session generation and must complete or fail using the same token/generation.
Property reads return cache snapshots and perform no I/O. A successful write
invalidates the cache until a later refresh. `accepted` and completed success
remain separate outcomes; no operation is automatically replayed on reconnect.

I64/U64, including enums with a 64-bit underlying type, cross the QML boundary as
canonical decimal strings. Keep those strings intact instead of converting them
through JavaScript `Number`. The generated QML module uses the C++ codecs rather
than JavaScript BigInt or a runtime descriptor parser. QML maps/lists are
snapshotted and checked before dispatch; conversion failures finish with
`Validation` without transport I/O.

Both modes use the optional generated `DeviceClientAdapter.hpp` and the SAME
portable `DeviceClient` execution FIFO (8 requests and a shared 4096-byte payload
pool by default). The adapter's token map only correlates QML completions; it is
not another execution queue. Full queues explicitly finish with `QueueFull`.
The older single-active `QmlRpcAdapter.hpp` remains an optional compatibility
adapter; this example uses the new common queue.

Default mode exchanges Encoded request/reply v1 packets through `EncodedBackend`
and the actual native `EncodedDispatcher`. RPC mode drives the existing
caller-buffered `RpcClient` through `RpcBackend`, one mailbox and
`RpcSessionPool<1>`, including fragmentation through the 48-byte packet buffer.
Both modes preserve routing, endpoint and codec outcomes. An Encoded response
confirms the reported outcome; this simple protocol supplies no server dedup
ledger or automatic retries. Existing RPC retries retain the same transaction.

A 1 ms event-loop timer drives the owner. The port first copies each complete
packet into bounded port-owned storage, calls `prepareHandoff(session,
requestId)` before sending, and reports `onSendAccepted` only after complete
handoff. Partial writes, DMA buffer release and an endpoint response are
separate events on a physical transport. All borrowed backend buffers outlive
the adapter. Native fixture callbacks are small synchronous work; slow callbacks
belong behind an application worker boundary with its own admission policy.

A successful final RPC WRITE reply receives the exact pool `replyAccepted`
ticket before dispatch. If recovery is required, the application drains cancelled
FIFO completions, excludes old packet copies, releases the actual old pool
handle, acquires a fresh connection, confirms closure and obtains/adopts a matching
header again. Local generation changes do not clear the server mailbox. No old
operation is resubmitted. This fixture cannot remain Running between loopback
ticks; a physical server must also drain a Closing callback before reuse.

Shutdown first stops the timer/producer, calls `close()` and continues owner
processing until `drained()`. Adapter destruction requires that drain and no
executing callback. Physical ports must also stop or drain their DMA/socket
copies. There is no blocking I/O or nested event loop.

This example binary links the installed native dispatch and RPC SDK targets.
The generated main QML module has no mandatory RPC dependency. See
[GeneratedQmlApi.md](https://github.com/shpegun60/telemetry-sdk/blob/main/doc/GeneratedQmlApi.md) for the transport and lifetime
contract.
