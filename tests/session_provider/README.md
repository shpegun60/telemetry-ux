# Session provider acceptance

`run.py` packs SDK and UX, installs both tarballs into an isolated consumer, and
strictly typechecks the public Session mounting journey against generated SDK
types. Supply `--sdk-package` to reuse an independently qualified SDK package.
The output directory must be fresh; reusing an old report invalidates its success.

The suite builds the existing `examples/web_demo/native` Model against an
installed C++ SDK and uses the installed generator with `--wire-editing`. It
does not duplicate the native descriptor, model, or Encoded protocol.

`Provider.mjs` tests Static and Dynamic real Sessions, one FIFO, independent
endpoint selectors and drafts, QueueFull refusal, accepted sibling invalidation,
explicit headless invalidation, exact U64/S64/unknown enum/signed-zero values,
untouched NaN payload bytes, automatic disconnect/reconnect, disposal, and pending
outcome ownership. ReadMany and Mixed Batch use the same real Session/FIFO and
the independently discovered TRM1/TBM1 records. The optional tests cover bounded
polling, skipped ticks, stop/drain, per-item statuses, exact scalar values,
QueueFull preservation, accepted mutation freshness, and delayed Watchlist or
read-only Batch responses after another panel mutates data. Missing optional
methods fail without I/O while explicit Individual FieldRead remains usable.
New admission restores neither a poll recipe nor a Batch, and a disposed editor
retains its uncertain operation facts without replaying mutations.

`Browser.py` runs Chromium against the unchanged HTTP/WebSocket demo server and
native dispatcher. It verifies full and standalone Session mounts, independent
editors, stale draft text, native readback normalization, exact large integer
editing, historical standalone facts with revoked values hidden, and owned versus
borrowed provider unmounts during accepted work. Actual Watchlist buttons produce
one ReadMany exchange or the explicitly selected Individual reads. The Batch
editor submits an exact-integer FieldWrite, Command, Service and FieldRead in one
exchange, verifies peer invalidation and independent item outcomes, and checks a
lost ACK/disconnect/reconnect without replay. Standalone Watchlist and Batch
panels mount directly from Session. This is host validation; it does not use
physical hardware or establish exactly-once execution.

The shipped `examples/session_web` application is staged unchanged and checked
in both modes for independent selection, one explicit native read, historical
facts after disconnect, no reconnect replay, and terminal close behavior.

Required runner options match `tests/web_configurator/run.py`: installed C++
`--sdk-prefix`, installed `--codegen-python`, Playwright/websockets
`--browser-python`, `--node`, `--npm-js`, `--tsc`, `--cxx`, `--cmake`, and `--ninja`.
Optional `--sanitize` enables address and undefined-behavior checks for the
existing native peer on a supporting toolchain.
