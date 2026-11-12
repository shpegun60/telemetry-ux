# Qt Inspector TCP checks

These checks use actual loopback `QTcpSocket` connections. The demo server routes
native Resource packets through the existing Resource protocol and TEQ1 packets
through the existing Encoded dispatcher. It introduces no outer frame header or
second execution queue. The Descriptor is discovered at file index 1 after a
decoy; the client never assumes that Descriptor is file zero.

```sh
python3 -B tests/inspector_tcp/run.py --build-dir /external/inspector-tcp \
  --sdk-prefix /external/sdk-prefix --qt-prefix /usr --cxx /usr/bin/g++ --cmake /usr/bin/cmake --ninja /usr/bin/ninja
```


The runner requires an installed SDK prefix explicitly through `--sdk-prefix`.
It passes that package's exact `Telemetry_DIR` to CMake and never reads an SDK
source tree. `summary.json` records SDKMetadata (version, source revision and
wire versions) separately from the UX revision, and fingerprints all files in
the actual installed prefix before and after execution. SDK or UX input drift
fails qualification. The prefix and fresh output directory must be disjoint.

Requires Qt 6.12 or newer with Network, Quick Controls and Test. On Windows select
the matching Qt MinGW kit explicitly. The runner puts that compiler and Qt runtime
first in `PATH`, keeps builds/logs outside the source tree, and retains before/after
source hashes, binary hashes and a machine-readable result. Source drift fails the
run. The maintained floor is 812 checks, 36 real FieldReads and 32 admitted socket
connections; refused discovery and extra raw socket connections are counted
separately in the scenarios.

Coverage includes fragmented request writes and response writes, paged Descriptor
reads, exact U64/S64 text, an unknown signed enum, nested structures, an empty
structure and a 16,384-element array. Additional cases cover old-ID and old-session
frames coalesced before a valid response, selection supersession, delayed replies
across reconnect, Descriptor replacement with the same endpoint IDs, partial
request/reply disconnects, timeout/recovery, missing or invalid Descriptor,
one-active-client admission, and invalid stream prefixes. The server's fault
controls make these boundaries deterministic; they do not assert how an operating
system happens to segment TCP traffic.

A complete checked response followed immediately by EOF must produce exactly one
successful completion before disconnect clears its displayed snapshot. A response
that misses the request deadline must not publish success. A correlated temporary
`Unavailable` result keeps the connection usable for the next read. Closing and
reconnecting the same transport object invalidates its old queued notifications.

After an uncertain handed-off operation, the controller keeps the closed/drained
old client until a valid Descriptor bootstrap from the same configured peer and
actual IP establishes old-context closure under the supported exclusive synchronous
server profile. A different peer is refused while this recovery obligation remains;
independent demo/file browsing does not erase it. An idle client without recovery
may be destroyed after local close/drain, and a different peer gets a new core.
No old binding is revived and no remote proof is inferred from local socket abort.

FieldWrite scenarios require explicit connection opt-in. They exercise partial
request writes, a normalizing setter followed by Refresh, setter refusal with no
readback, confirmed Write followed by refused Refresh, and a lost Write ACK with
exactly one native setter call. Reconnecting the supported same peer never replays
that write. A complete successful Write response followed by EOF remains a known
successful write with unavailable readback, rather than becoming OutcomeUnknown.

Explicit Disconnect is tested after native setter execution with a lost Write
ACK, and after the Workbench enters Refreshing following a confirmed Write.
Both emit one terminal Apply result. Repeated Disconnect and same-peer reconnect
must not repeat the callback or invoke the native setter again.

Command scenarios use the same socket, opt-in and FIFO. Separate native counters
distinguish Executed and Accepted. A dropped acknowledgement is followed by
explicit Disconnect only after the native Command counter proves handoff and
execution. The controller must report one unknown result and no replay on
reconnect. Known terminal results retain their status and session of origin.
Closing this synchronous protocol context does not establish completion of a
separate application job returned as Accepted.

Service scenarios use actual request/response DTOs and a separate native call
counter. They distinguish a successful owned response, InvalidArgument and a
lost acknowledgement followed by Disconnect. Terminal signals are delivered
once; reconnect cannot replay a call or reinterpret a response from an old session.
These cases use the same connection, FIFO and mutation opt-in as Field/Command.

The runner also starts the real demo server executable on an ephemeral loopback
port, opens the actual QML application with `--tcp`, and requires a published
Field plus visible decoded value delegates. It retains the rendered PNG and
both process logs, then terminates the persistent demo-listener process. Object
close/drain behavior is exercised by the maintained in-process socket scenarios.

`--sanitize --cxx clang++-18` enables fatal ASan/UBSan checks on Linux. Leak checking
is disabled for Qt's process-global offscreen plugin allocations. This is host
socket qualification, not STM32, UART, DMA, D-cache or physical link qualification.
