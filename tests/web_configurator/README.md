# Installed browser Configurator qualification

The gate builds the demonstration's native Model against an installed C++ SDK,
packs Telemetry UX and installs it with an SDK npm tarball in a fresh external
consumer. Generation uses the installed matching Python distributions. It has
no SDK source-tree imports or generated output inside either repository.

```sh
python3 -B tests/web_configurator/run.py --build-dir /external/web-check \
  --sdk-prefix /path/to/installed-sdk --sdk-package /path/to/telemetry-sdk-0.1.0.tgz \
  --codegen-python /path/to/codegen-env/bin/python \
  --browser-python /path/to/playwright-env/bin/python --node /path/to/node \
  --tsc /path/to/typescript/bin/tsc --cxx g++
```

Supply Playwright 1.63.0 and its Chromium runtime, TypeScript 5.9.3, Node 22,
CMake/Ninja and a C++20 compiler. The Node distribution's npm CLI is located
automatically, or can be selected with `--npm-js`. The runner installs only the
two supplied local npm tarballs with scripts disabled and offline mode. Its npm
cache, generated SDK, builds, screenshots and logs stay in the output directory.
Use `--sanitize --cxx clang++-18` to instrument the native demo consumer with
ASan/UBSan/LSan; the prebuilt SDK archive retains its own instrumentation.

The maintained checks cover:

- A strict TypeScript consumer of the installed public API, plus intentional
  negative type controls and generated starter declarations.
- Real static and dynamic providers through one existing SDK client per mode,
  persistent native Encoded dispatch, owned drafts, refused QueueFull submissions,
  stale revisions, subscriber reentry and separate Write/readback outcomes.
- Exact untouched F32/F64 NaN wire bits, both signs and signaling/quiet variants,
  preserved through another member's edit; draft seed ownership and old-codec refusal.
- Chromium keyboard/input/pointer actions for exact U64/S64, overflow,
  intermediate text, Apply without Enter, device normalization, Accepted versus
  Executed, Service refusal, Void versus empty structure, nested values and
  detached old inputs.
- A Write executed by the Model while its ACK is retained, followed by explicit
  Disconnect, `OutcomeUnknown`, stale-value retirement, late response and
  reconnect without replay.
- Real Static/Dynamic ReadMany over four selected scalar Fields within the
  provider's 32-Field capacity, session-bound
  capabilities, exact U64/S64/open enum and per-item Unavailable. Deterministic
  clocks test skipped ticks, pending Stop/drain, malformed replies, QueueFull,
  stale completions and explicit Individual fallback. The native provider floor
  is 871 checks. Chromium also selects four Fields through the actual Watchlist
  controls and proves one batch exchange versus four explicit individual reads.
- Pure renderer controls for 128→256→300 rows, exact 128-row windows, safe text,
  keyboard focus, unsubscribing once and preserving a host-owned sibling/client.
  Detached expand and pagination controls cannot change a new selection or an
  unmounted view.
  These small presentation controls use a provided model; the primary two
  client journeys always use the actual native SDK dispatcher.

`summary.json` identifies UX inputs, both package archives, generated output,
installed SDK version/revision/files, tools, native binary, browser version and
command logs. Source and installed-prefix drift fail qualification.
The native configure pins the selected `Telemetry_DIR`, disables package registries
and verifies its cache while a real competing `Telemetry_ROOT` is present.
The browser retains screenshots and rejects console warnings and page errors. Native exit
status and stderr are checked after cleanup, including the sanitizer profile.
These are host software tests, not physical-board or transport qualification.
