# Persistent native browser-demo peer

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This application-specific process owns a simulated native Model. It links only
installed SDK 0.1.0 `core`, `encoded_server` and `batch_server` components. There is no Qt,
Resource, COBS, client queue or SDK source checkout dependency. It is not a
production HTTP server or physical-device transport.

```sh
cmake -S examples/web_demo/native -B /tmp/web-demo-native \
  -DCMAKE_PREFIX_PATH=/path/to/installed-sdk
cmake --build /tmp/web-demo-native
/tmp/web-demo-native/telemetry_web_demo_peer --descriptor /tmp/descriptor.bin
/tmp/web-demo-native/telemetry_web_demo_peer
```

The interactive process persists application state across requests. Each input
line is one lowercase hexadecimal, complete TEQ1 request; each output line is
the actual SDK dispatcher's complete reply encoded as lowercase hexadecimal.
Operations 1–4 use the existing dispatcher with separate 64 KiB request, response
and Workspace buffers. No reply/header/status is synthesized by the demo. A
malformed line exits with code3; an envelope/admission/capacity dispatch refusal
exits with code4 rather than inventing a reply. Model-level endpoint refusals
remain ordinary correlated SDK replies. Every successful output is flushed.

Only these out-of-band test controls are accepted:

- `state` prints JSON counters and the current Config. U64/S64 are decimal
  strings; nonfinite scalar values are JSON null in this diagnostic only.
- `reset` restores initial Config and clears counters, acknowledging `{}`. It
  does not change the trusted session.
- `session N` admits a nonzero decimal U32 session and acknowledges `{"session":N}`.
  The initial trusted session is17. Request headers never choose this value.
- `readmany` and `mixedbatch` return independently admitted, session-bound
  `TRM1`/`TBM1` capability bytes as lowercase hexadecimal.

The Descriptor has two Field catalogs: `control.Config` is writable,
`control.ReadOnly` is read-only, and `diagnostics.Counters` reports reads, writes,
commands and services. Reading Counters counts as one read. Config includes all
eleven scalar codes, exact U64MAX/S64MIN, F64 negative zero, a signed open enum
with named values -7/12 and initial unknown code -123, and a nested 2×2 array.
Writes and SetConfig normalize gain above10 to10. These are application rules,
not defaults or constraints exported by the Descriptor.
Scalar diagnostic Fields Wide, Signed, Mode and Unavailable support Watchlist
examples without treating nested DTO members as independent Field IDs.

Commands are SetConfig(Config)→Executed, Accepted(Void)→Accepted without changing
Config, and Reset(Void)→Executed. Services are Echo(Config)→Config, Inspect(Void)
→current Config, Ping(Void)→Void and Empty(Void)→an empty Struct. Echo refuses
negative gain with InvalidArgument. Repeated requests may execute repeatedly;
this peer has no retry, duplicate suppression or exactly-once guarantee.

## Optional mixed Batch

The peer explicitly admits `TBM1` independently of `TRM1`. Its trusted stdin
owner can request the current capability with `mixedbatch`; receiving opcode 6
does not choose or replace the admitted session. The host exposes this record
only after its connection/bootstrap policy accepts the peer.

The Batch editor creates new canonical request DTOs. A FieldWrite item replaces
the whole Field; it is not a preserving edit of a previous read. Initial zero
values are not device defaults. Items run in order, with individual outcomes,
without rollback, duplicate suppression, automatic replay or automatic readback.
The native consumer therefore requires the installed SDK `batch_server` component.
