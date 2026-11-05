# Qt/QML Universal Inspector and Workbench

This application displays a Descriptor v3 without a generated SDK. It builds on
the portable [InspectorModel](https://github.com/shpegun60/telemetry-sdk/blob/main/doc/InspectorModel.md), keeps one selected
Field snapshot, and uses the existing DynamicClient / DeviceClient FIFO.

The default **Demo device** is an in-process native Telemetry Model. Refresh
passes actual Encoded request/reply packets through its dispatcher. Demonstration
values are not measurements from a connected controller. Importing a Descriptor
file enables metadata browsing only; its endpoint IDs are never sent to the demo.

The [scalar Watchlist](https://github.com/shpegun60/telemetry-sdk/blob/main/doc/ReadManyWatchlist.md) adds bounded periodic reads
of up to 32 selected Fields. Use **ReadMany demo**, or explicitly enable TRM1
discovery for the UART ReadMany firmware. **Individual FieldRead** is an explicit
fallback for older devices. Missed polling ticks are skipped; foreground
snapshots and editing drafts keep their own state.

Use **Connection settings** to show TCP, UART and Descriptor-file controls.
They collapse after connection and reopen on disconnect, leaving room for the
endpoint tree and values. Watchlist status messages wrap below its actions.

## Build and run

Use a matching Qt 6.4-or-newer C++20 kit with Core, Gui, Qml, Quick,
Quick Controls 2 and Network, plus CMake and Ninja. The application requires an
installed Telemetry SDK 0.1.0 prefix containing `inspector`, `workbench`,
`read_many_workbench`, `encoded_backend`, `encoded_server`, `core`, `tcp_server`
and `qt_transport`. Optional UART support also requires the installed
`qt_serial_transport` component and matching Qt SerialPort.

The user build has four steps:

1. Install the matching SDK and Qt kit once. The SDK producer handles its private
   COBS build dependency; this application does not need that checkout.
2. Configure from the Telemetry UX root, using an external build directory:

   ```sh
   cmake -S apps/inspector -B /tmp/telemetry-inspector-qml -G Ninja \
     -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="/sdk-prefix;/qt-prefix"
   ```

3. Build: `cmake --build /tmp/telemetry-inspector-qml --parallel 2`.
4. Launch: `/tmp/telemetry-inspector-qml/telemetry-inspector-qml`.

The default build provides the local demo, Descriptor files and TCP. Add
`-DINSPECTOR_ENABLE_SERIAL=ON` for the optional UART adapter. CMake requires all
selected installed components and never falls back to an SDK source checkout.
In Qt Creator, open this directory's `CMakeLists.txt`, choose the matching kit,
and add the SDK prefix to `CMAKE_PREFIX_PATH`. The Widgets playground is a
separate application.

### Work on the application in Qt Creator

Open **`apps/inspector/CMakeLists.txt`** from your Telemetry UX checkout. This is
the application's source project; the Windows ZIP is an optional distribution,
not a prerequisite for editing or running it.

1. Choose your Qt 6.4+ C++20 kit. Its compiler/ABI and Qt version must match the
   installed SDK prefix, including any optional transport libraries.
2. Choose a build directory outside the checkout. In **Projects → Build → CMake**,
   append the SDK installation prefix to `CMAKE_PREFIX_PATH`; keep the kit's Qt
   prefix. Enable `INSPECTOR_ENABLE_SERIAL` only if both the SDK serial component
   and the matching Qt SerialPort module are installed.
3. Configure, build, select the **`telemetry-inspector-qml`** run target and run it
   with no arguments. **Demo device** works without a board. Set a breakpoint in
   `InspectorController.cpp` or edit `Main.qml` and rebuild as usual.

Qt Creator supplies the selected kit's Qt runtime. If SerialPort was installed
in a separate prefix on Windows, also add that prefix's `bin` directory to the
run environment's `PATH`. No `windeployqt`, ZIP extraction or Python is required
for this development workflow. Keep kit-specific settings in a local
`CMakeUserPresets.json` or Qt Creator `.user` file; both are Git-ignored.

For later shell examples set
`inspector_exe=/tmp/telemetry-inspector-qml/telemetry-inspector-qml`.
On Windows, use one matching kit and put its compiler before other GCC toolchains:

```powershell
$QtPrefix = "C:\Qt\6.10.1\mingw_64"
$QtTools = "C:\Qt\Tools"
$SdkPrefix = "C:\external-sdk\telemetry-sdk"
$InspectorBuild = "C:\external-build\telemetry-inspector"
$CMake = "$QtTools\CMake_64\bin\cmake.exe"
$env:PATH = "$QtTools\mingw1310_64\bin;$QtPrefix\bin;$env:PATH"
& $CMake -S apps/inspector -B $InspectorBuild -G Ninja `
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$SdkPrefix;$QtPrefix" `
  "-DCMAKE_CXX_COMPILER=$QtTools/mingw1310_64/bin/g++.exe" `
  "-DCMAKE_MAKE_PROGRAM=$QtTools/Ninja/ninja.exe"
& $CMake --build $InspectorBuild --parallel 2
$InspectorExe = "$InspectorBuild\telemetry-inspector-qml.exe"
& $InspectorExe
```

These commands use the Qt runtime through this shell's `PATH`; they do not
modify Qt or create a standalone deployment. In PowerShell use `& $InspectorExe`
with the same arguments as the later `"$inspector_exe"` shell examples.

Expand **Fields**, then a catalog. Select a Field and press **Refresh**. Selecting
does not issue a request. Commands provide argument editing and Execute;
Services provide explicit Call and an owned, read-only Response on writable
Demo/TCP profiles.
Large aggregates fetch at most 128 immediate children per call; **Load next 128**
adds another batch. Expanding a nested aggregate fetches its own first batch.
There is no recursive expand-all operation. Fetched rows are retained until the
snapshot is retired, so viewing every element can still consume proportional Qt
memory.
This example owns at most 65,536 bytes per Field snapshot. Larger payloads are
refused within that bound; metadata can still be browsed.

To export or inspect a reproducible Descriptor:

```sh
"$inspector_exe" --write-demo-descriptor /tmp/demo-descriptor.bin
"$inspector_exe" --descriptor /tmp/demo-descriptor.bin
```

The demo exposes `measure.Voltage`, exact `device.Counter`/`device.Offset`, an
unknown signed enum, a 16,384-element array, an empty struct and nested arrays.

## Read, edit, write and refresh

Choose **Writable demo** for a local editable Model, expand Fields / device and
select `Config`. Press **Refresh**, then **Edit**. Edit scalar leaves of the
structure or its arrays as text. Valid keyboard input updates the draft while
you type; **Apply + Refresh** includes the focused editor without requiring Enter.
**Cancel**
discards the draft. **Apply + Refresh** writes once, then reads the device again;
the displayed result comes from that readback. For example, the demo setter
limits `Gain` to 10, so entering 12 shows 10 after a successful Apply.

The default connection starts with mutations disabled. For TCP, enabling changes
is an explicit opt-in, cleared on reconnect. The Descriptor must
also advertise write permission. Imported files provide metadata only. The
current UART/Nucleo profile remains read-only, even if imported metadata contains
writable Fields, Commands or Services.

U64/S64 and enum codes stay decimal strings; they never pass through QML Number.
Boolean input accepts `true`, `false`, `0` or `1`. Invalid text or an out-of-range
number leaves the draft unchanged and disables Apply until corrected. Unedited
floating-point bytes, including NaN payloads and negative zero, remain intact.
This example permits editing at most 4096 bytes per value; larger read snapshots
remain browsable up to the existing 65,536-byte bound.

A confirmed Write followed by a failed Refresh is reported as applied with
unconfirmed readback. An uncertain Write is never automatically replayed. The
normal transport recovery policy still applies. This is read-modify-write,
without a compare-and-write guarantee against another writer.

Explicit Disconnect completes an accepted Apply once: Cancelled before handoff,
OutcomeUnknown after uncertain handoff, or a known successful Write with
unconfirmed readback. It clears the cache and preserves the terminal write
message. The controller also retains these outcomes during connection replacement;
it never replays the write as part of reconnect.

After a new session is admitted, the retained outcome is labelled
`Previous session X (current session Y)`. Its original result, including
OutcomeUnknown, remains visible as history. A newly accepted Apply replaces
that history with its own Pending/result; a refused Apply leaves it intact.

The portable [Workbench contract and API](https://github.com/shpegun60/telemetry-sdk/blob/main/doc/Workbench.md) describe its
single-client ownership, draft storage and independent operation results.

## Command arguments and Execute

Choose **Writable demo**, expand Commands / control and select `Execute`.
The form is prepared locally from the Descriptor; selecting it sends nothing.
Edit scalar leaves with the same text controls as FieldWrite, then press
**Execute**. The focused input is included without Enter. Initial arguments are
canonical zero values, not defaults supplied by the device. Open enum zero may
therefore appear as an unknown code. Unchanged arguments can also be executed.

The demo includes `Accept`, `Ping` (no arguments) and `Empty` (an empty Struct).
`Executed` confirms endpoint execution. `Accepted` only confirms acceptance of
work and does not claim it has finished. A Command has no automatic Field refresh
or automatic second execution. `OutcomeUnknown` means it may have run; reconnect
does not replay it. Disconnect and connection replacement preserve one terminal
result, labelled with its original session when another session is admitted.

Command execution uses the same explicit connection opt-in and supported
Demo/TCP profiles as FieldWrite. Descriptor files remain metadata-only and the
current UART/Nucleo profile remains read-only. Command arguments are shown as a
request form, separately from the last Field snapshot.

## Service requests and responses

Choose **Writable demo**, expand Services / control and select `Transform`. Its Request is
prepared locally with the same canonical initializer and scalar editor as Command.
Enter arguments and press **Call**; the focused editor is included without Enter.
The separate, read-only Response tree uses the same lazy navigation for nested
structures and arrays. U64/S64 remain exact text, without conversion to QML Number.

The response is owned by the Workbench before completion is delivered. A Void
response has no value tree, while an empty Struct is a valid zero-byte value.
Endpoint InvalidArgument is a rejection, never Command Accepted. The result and
last Response identify their originating endpoint/session, even when another
request form is selected. Editing a new Request does not execute it.

Service calls require the same explicit mutation opt-in as FieldWrite/Command
on the Demo/TCP profiles. Imported Descriptors are metadata-only and the current
UART/Nucleo profile remains read-only. Disconnect preserves one terminal result;
OutcomeUnknown requires recovery and never triggers an automatic call or Field
Refresh. The native demo counter and real TCP tests check that reconnect does
not replay a Service.

## TCP connection and Descriptor discovery

Enter the device address and TCP port in the window, or launch directly:

```sh
"$inspector_exe" --tcp 10.50.0.0:5577
```

Use the controller's actual configured address. The application opens one
asynchronous `QTcpSocket`, enumerates Resource files with LIST, finds the exact
path `/telemetry/descriptor.bin`, checks STAT and downloads the complete file
with READ. The file index is discovered, never assumed to be zero. Only a valid
Descriptor is admitted. Discovery has a 10-second exchange timeout and a
60-second overall limit. Missing paths, malformed lengths, unexpected tails,
EOF or timeout close the connection without automatic packet replay.
READ cursors are forwarded as opaque provider tokens, including nonmonotonic
values. Accumulated payload bytes must fit STAT and match its size at EOF.
Cursor-only progress is supported with a limit of 4096 such replies per file;
it does not extend the absolute discovery deadline.

Subsequent Refresh requests use the existing `EncodedBackend` and Inspector
FIFO. TCP fragments are accumulated into complete existing TEQ1 frames; no
additional wire header, RPC layer or execution queue is introduced. A request
is copied before `prepareHandoff()`, then only its unsent tail is written. Qt
owns each accepted socket-write copy. Replies retain their received session and
request ID; old identities cannot be relabelled as the current request.

The supported server profile is a direct, single-active-client connection with
synchronous dispatch. The server must finish dispatch and release the previous
connection before serving a new client's Descriptor. Both the included host
server and the STM32 integration follow this profile. Concurrent multi-session
servers, proxies that switch remote identities and authentication are outside
this example's contract.

Run a reproducible local TCP device in a second terminal:

```sh
"$inspector_exe" --serve-demo 5577
# In another terminal, with inspector_exe set to the same executable:
"$inspector_exe" --tcp 127.0.0.1:5577
```

The demo listener binds only to loopback. `--serve-demo 0` selects and prints an
ephemeral port. It uses the real native Model, Resource file providers and
[TCP server adapter](https://github.com/shpegun60/telemetry-sdk/blob/main/lib/integration/tcp/README.md); its values remain
demonstration data. The optional serial profile is described below.

## NUCLEO-H7S3L8 through ST-LINK serial

The optional serial build uses the installed SDK Qt serial adapter. Its public
headers and libraries are sufficient; no COBS source path is supplied here.
The SDK's [Qt transport module documentation](https://github.com/shpegun60/telemetry-sdk/blob/main/doc/QtTransport.md)
describes the producer-side dependency.

```sh
cmake -S apps/inspector -B /path/to/external/inspector-serial -G Ninja \
  -DCMAKE_PREFIX_PATH="/sdk-prefix;/qt-prefix" \
  -DINSPECTOR_ENABLE_SERIAL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/external/inspector-serial --parallel 2
/path/to/external/inspector-serial/telemetry-inspector-qml \
  --serial /dev/ttyACM0 --baud 115200
```

On Windows, reuse the Qt/tool/SDK variables above and select a fresh build:

```powershell
$SerialBuild = "C:\external-build\telemetry-inspector-serial"
& $CMake -S apps/inspector -B $SerialBuild -G Ninja `
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$SdkPrefix;$QtPrefix" `
  "-DCMAKE_CXX_COMPILER=$QtTools/mingw1310_64/bin/g++.exe" `
  "-DCMAKE_MAKE_PROGRAM=$QtTools/Ninja/ninja.exe" `
  -DINSPECTOR_ENABLE_SERIAL=ON
& $CMake --build $SerialBuild --parallel 2
$InspectorExe = "$SerialBuild\telemetry-inspector-qml.exe"
& $InspectorExe --serial COM6 --baud 115200
```

If matching Qt SerialPort is installed in a separate external prefix, add
`-DQt6SerialPort_DIR=C:/external-tools/qtserialport/lib/cmake/Qt6SerialPort`
and prepend its `bin` directory to `PATH` before launching. Keep that module
external; no copying into `C:\Qt` is required.

`COM6` and `/dev/ttyACM0` are examples; select the actual ST-LINK port in the window.
UART controls appear only in a build configured with `INSPECTOR_ENABLE_SERIAL=ON`.
The matching [Nucleo firmware](https://github.com/shpegun60/telemetry-sdk/blob/main/examples/nucleo_h7s3l8/README.md) serves demonstration
values through USART3 DMA. Both peers use
[Profile.hpp](https://github.com/shpegun60/telemetry-sdk/blob/main/lib/integration/serial/Profile.hpp): existing COBS framing/CRC
with at most 2048 body bytes. Each body is an ordinary Resource packet or complete
TEQ1 message, without an extra application route. Descriptor and TEQ1 formats
are unchanged. Field payloads above 2016 bytes remain browsable as metadata but
Refresh refuses them explicitly before changing the snapshot or FIFO.

Only complete, CRC-checked packets reach the Resource/Encoded readers. A short
serial write closes the failed exchange; the library does not replay a prefix
or retry the request. The supported host baud range is 1200 to 4,000,000, 8N1;
the peer must use the same rate. Automatic exchange deadlines include the
worst-case framed request and response airtime plus 5 seconds for processing
and scheduling. Bootstrap reserves a maximum-size response. Field-read enqueue
deadlines include the selected Field's airtime plus 3 seconds; even a maximum
2016-byte payload at 1200 baud fits within the backend's 30-second cap.
`SerialTransport::setTimeout()` and `InspectorController::setReadTimeout()`
instead set explicit absolute deadlines for future operations, without adding
airtime; reads remain subject to the backend's 30-second cap. Neither default
nor explicit deadlines restart on arriving bytes.

Total discovery has a separate 180-second absolute limit, so very large
Descriptors at low baud can still exceed this example's bounded discovery
profile. Supporting the maximum Descriptor size does not promise that it can
be transferred at every selectable baud within that limit.
The exclusive host sends one request at a time and never runs another serial
producer on the same port.

Recovery requires the supported synchronous, read-only board profile: dispatch
waits for the previous UART transmission and COBS lease to finish, then processes
RX in order. A complete, validated new LIST/STAT/Descriptor transcript from the
same fixed peer is the sequencing barrier. COM close/clear and an isolated LIST
reply do not establish it. USB identity is captured when the port opens;
replacing the MCU or its wiring while recovery is pending is outside this
profile. Malformed or stale bootstrap packets close the attempt without new
admission. This does not qualify a general asynchronous serial server or RPC.

The read-only headless check also uses the real Controller/Qt models:

```sh
"$inspector_exe" --serial /dev/ttyACM0 --baud 115200 --check-device \
  --report /path/to/external/device-report.json
```

With the serial Windows build, use:

```powershell
& $InspectorExe --serial COM6 --baud 115200 --check-device `
  --report C:\external-build\device-report.json
```

It reads all demo Fields, preserves integer values as strings, verifies all
three F32 voltages, the exact U64 Counter, demonstration flag, nested matrix,
boolean and unknown signed enum. Run it on the Nucleo demo firmware.
The [guarded hardware runner](https://github.com/shpegun60/telemetry-sdk/blob/main/tests/nucleo_h7s3l8/README.md)
backs up and restores the original internal Flash. Host PTY tests are maintained
in [inspector_serial](https://github.com/shpegun60/telemetry-sdk/blob/main/tests/inspector_serial/README.md).
The [published Nucleo evidence](https://github.com/shpegun60/telemetry-sdk/blob/main/doc/evidence/inspector-serial-h7s-2026-10-28/README.md)
includes a physical four-Field serial check and the QML window. It qualifies the
captured board/profile and source inputs; support for another board or transport
still needs its own checks.

## Ownership and shutdown

The controller owns descriptor bytes, runtime indexes, the workspace control
object, backend storage and one portable Workbench containing the sole Inspector
and DynamicClient. Models copy labels and scalar
text into Qt-owned storage; they retain sealed handles, never borrowed callback
views. U64/S64 and enum codes remain decimal strings all the way into QML.
Floating-point display preserves negative zero and identifies NaN; display text
does not preserve every NaN payload bit.

An accepted Refresh retires the previous snapshot. Rejected submission leaves
the current Inspector snapshot intact. Schema/session invalidation clears old
values. Changing the displayed Field supersedes an old read instead of publishing
it under the new selection.

Watchlist invalidation also notifies existing QML delegates after foreground
transport failures. A logical read cycle stays active while paused behind
foreground work, so its selection, mode and frequency cannot change mid-cycle.
Stop revokes future reads; an accepted read still drains before these controls
become available again.

Qt model reset/insertion signals may run application slots synchronously. Both
models guard their mutation phases; controller reconnect, disconnect and refresh
are refused during those phases. Completion signals run after returning from the
portable client. All calls belong to the Qt owner thread.

On replacement/shutdown the transport drops deferred reply copies and completes
logical close/drain before freeing schema storage. The demo port releases TX
synchronously when dispatch returns. TCP abort releases local socket-owned
storage; it does not prove remote session closure. An uncertain handed-off read
retains its closed, drained Inspector until a fresh Descriptor bootstrap from
the same requested host/port and actual peer IP establishes remote cleanup
under the single-active-client contract. Only then is `confirmQuiescence()`
called before replacing that state. Switching to another peer is refused while
this recovery remains unresolved. Idle completed clients can be destroyed after
local release; another device receives an independent core and binding.

Process shutdown can discard the retained recovery record after local release
and logical drain; it never reports that as remote closure or successful
operation completion. A physical UART/DMA port would need separate evidence
that its driver has stopped reading borrowed buffers.

## Checks

The [Qt test suite](../../tests/inspector_qml/README.md) checks the actual models,
controller and peer. `--smoke` loads the real QML window, performs a FieldRead and
exits successfully only after a published snapshot and materialized endpoint/value
delegates. `--smoke --screenshot FILE.png` also saves the rendered window.
Smoke modes use a fixed 1200-by-900 viewport so optional UART controls leave the
required value delegates visible; the normal application window is unchanged.
Windows offscreen qualification supplies the OS font directory explicitly; the
normal native window uses platform fonts. Offscreen CI does not qualify every display
server or physical hardware.

`--edit-smoke --screenshot FILE.png` exercises the actual QML Edit button,
TextField handler and Apply button against the writable native demo. Success
requires one Write and a published readback with the normalized Gain value 10.

The [TCP suite](../../tests/inspector_tcp/README.md) additionally runs real
loopback sockets, fragmented discovery and reads, disconnects, partial writes,
timeouts, stale replies, Descriptor replacement and a separate-process server
with the actual QML window. These host results and offline firmware compilation
do not establish Ethernet/DMA behavior on a connected STM32.
