# Generated Qt/QML qualification

The runner generates an SDK from the UX-owned native demo descriptor, compiles its Qt
module, imports and instantiates it in an actual `QQmlEngine`, and exercises both
native Encoded calls and the real Resource/RPC mailbox. It runs offscreen with a
software Qt Quick backend. No hardware or network connection is used.

Prepare an installed Telemetry SDK `0.1.0` prefix containing `core`,
`encoded_server`, `encoded_backend`, `rpc_backend` and `rpc_sessions`, plus a
Python environment containing matching installed `telemetry-codegen-core` and
`telemetry-ux-codegen` packages. The runner requires both locations explicitly.
It never reads SDK sources or tests and never compiles SDK implementation files.
The shared [demo fixture](../../examples/codegen_qml/fixture) owns only application
DTOs, Model declarations and a native binary exporter; the installed SDK supplies
the one Model/dispatcher/client implementation.

```sh
python3 -B tests/codegen_qml/run.py --build-dir /external/qml-checks \
  --sdk-prefix /external/sdk-prefix --codegen-python /external/codegen-env/bin/python \
  --qt-prefix /usr --cxx g++ --cmake cmake --ninja ninja
```

For Clang ASan/UBSan, add `--sanitize --cxx clang++-18` and use an SDK prefix
built with the matching sanitizer flags. Generation and Python tests run from
external working directories with `PYTHONPATH` removed; package identities and
actual installed module hashes are recorded before and after qualification.

From the repository root on Windows:

```powershell
python tests/codegen_qml/run.py `
  --build-dir C:/Users/admin/.codex/artifacts/telemetry-qml-2026-10-18/windows `
  --sdk-prefix C:/external/sdk-prefix `
  --codegen-python C:/external/codegen-env/Scripts/python.exe `
  --qt-prefix C:/Qt/6.12.0/mingw_64 `
  --cxx C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  --cmake C:/Qt/Tools/CMake_64/bin/cmake.exe `
  --ninja C:/Qt/Tools/Ninja/ninja.exe
```

Use the compiler distributed for that Qt build. The runner puts its directory
before other MinGW installations in `PATH`. On Linux, pass the Qt CMake prefix
and explicit compiler/CMake/Ninja paths. Qt 6.12 or newer with Core, Gui, Qml,
Quick, and Test is required. Example validation additionally uses QuickControls2.

The checks cover asynchronous tokens, direct and RPC requests for all four
operations, exact native payloads, accepted work, failed statuses before payload
decoding, valid empty aggregates, malformed responses, cached property NOTIFY,
getter reads without I/O, canonical 64-bit decimal strings, nonfinite floating
point values, negative zero, actual QML object conversion/refusal, session
admission/reset, stale and duplicate completion, endpoint Busy, and bounded
timeouts. RPC storage forces request/result fragmentation and wrong-generation
replies are rejected. The admission check compares the exact declared 64-byte
header; it does not authenticate a peer or hash a remote descriptor body.

Known-result regressions reset or readmit the session between checked completion
and queued public delivery. Executed and Accepted results retain their status
and decoded values, including through both generated adapters. Separate cache
checks prevent old reads from populating a new session and old writes from
invalidating its confirmed value, including during nested event processing.

The queued executable additionally injects RetryingEncodedBackend into the same
generated adapter. A dropped TEQ1 reply causes byte-identical same-ID retransmission
and two actual native command calls, with one public completion. The checks cover
all four operations, Accepted versus Executed, QueueFull, normal cache behavior,
fresh retry handoff permission, and reset/recovery without automatic replay.

`summary.json` records commands, return codes, source hashes and timestamps;
individual logs contain build and runtime evidence. Outputs must be outside the
repository and the SDK prefix. It records the installed `SDKMetadata.json`, all
prefix file hashes, core/UX Python distribution hashes and executed binary
hashes. CMake receives one explicit `Telemetry_DIR`; a real alternate SDK prefix
in `Telemetry_ROOT` must not override it. Compiler input manifests reject SDK
source compilation. The conversion/macro probes independently compile only
generated codecs and Qt, without native SDK headers or libraries. Nine naming
and ownership unit tests use an explicit UX-owned descriptor builder and the
installed public `load_schema` export/CLI, without SDK test helpers or private
parser/generator imports. A passing host run does not qualify hardware.
