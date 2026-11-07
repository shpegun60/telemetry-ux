# Sequential host Watchlist scaling

This installed-package suite adds an opt-in 65-Field catalog to the maintained
native Web demo Model. `Peer.cpp`, Encoded parsing/dispatch, session records and
the reference WebSocket server are reused unchanged. The normal demo profile
remains OFF; its actual Descriptor SHA-256 is checked against the qualified
default bytes before the scaling profile is built.

`Scaling.mjs` uses actual Static and Dynamic connection Sessions and checks
0/1/32/33/65 selected Fields. A 65-Field host list produces ordered ReadMany
requests of 32, 32 and 1 IDs, or 65 sequential FieldRead requests in explicitly
selected Individual mode. It verifies exact U64 text, an Unavailable item in the
middle, one physical exchange at a time, integer intervals from 1 to 60,000 ms,
skipped ticks, Stop/drain, late replies, accepted peer mutations and a middle
transport failure. No endpoint request is created by selection or reconnection.
Reduced discovery limits independently constrain chunks to seven items, three
IDs by request capacity, and two U64 values by worst-case reply capacity. A
valid capability whose reply cannot hold even one selected scalar is refused
without issuing a request.

Publication is per successfully validated chunk, not a simultaneous or atomic
65-Field device snapshot. Stop keeps an already completed prefix and blocks
later publication/handoff. A failed or session-invalidated cycle retires its
displayed values. An accepted mutation invalidates sibling readings; ReadMany
itself remains read-only and never becomes OutcomeUnknown.

Live IPC phases wait for explicit held-response or provider publication events,
not a machine-speed-dependent number of event-loop turns. The shared test-only
publication fence observes readiness after a publisher finishes its stack and
uses a real 30-second watchdog independent of the frozen SDK clock. Peer exit or
error rejects the phase immediately. Bounded driver cleanup releases held work,
terminates only its own failed native subprocess and preserves the first test
failure. Shared lifecycle/cleanup controls run in the Session provider suite.

`Browser.py` loads 65 actual rows, changes the real interval input to 1 ms,
executes one three-chunk Refresh, starts repeated automatic Session cycles,
stops/drains and removes a polling view. Explicit `autoPoll:false` and advanced
provider mounts remain manually driven. Both Session modes run against a real
local WebSocket/native Model with strict browser diagnostics.
Long lists use an independently scrollable, keyboard-focusable values pane;
the browser test reaches its last rows with End while Start remains visible.
A one-millisecond requested interval is not a guarantee of 1,000 complete
device cycles per second: each cycle waits for its bounded sequential requests.

Run `run.py` with fresh external `--build-dir`, an installed C++ `--sdk-prefix`,
matching packed SDK or `--sdk-root`, installed `--codegen-python`, Playwright and
websockets `--browser-python`, `--node`, `--npm-js`, `--cxx`, `--cmake` and
`--ninja`. `--sanitize` instruments the native peer on a supporting compiler.
The runner records commands, hashes and source/installed-package stability.
It does not use a board or establish physical UART, DMA or Ethernet timing.
