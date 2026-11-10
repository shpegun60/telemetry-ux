# Controlled Dynamic Watchlist checks

This installed-package consumer exercises the same local scheduler and row model
used by `QtConnectionBinding` providers. Its source is a controlled completion
adapter; it creates no transport, native client or alternate FIFO. The real
Session/TCP/QML scenarios remain in `tests/qt_connection` and the standalone Qt
pair.

Configure and run against freshly installed SDK/UX packages:

```sh
cmake -S tests/dynamic_watchlist -B /external/watchlist-check -G Ninja \
  -DCMAKE_PREFIX_PATH="/external/sdk-prefix;/external/ux-prefix"
cmake --build /external/watchlist-check
ctest --test-dir /external/watchlist-check --output-on-failure
```

The SDK installation needs `workbench`; UX needs `dynamic_configurator`. The
tests manually complete each accepted read and invoke scheduling ticks, so they
do not infer a handoff from elapsed wall-clock time. They check:

- 65 rows read as 32 + 32 + 1 with ordered IDs.
- Smaller advertised request/reply limits and a next item that cannot fit.
- One accepted request, no Qt model notifications from its native callback,
  and no accumulation during busy ticks.
- Explicit Individual FieldRead and absence of automatic fallback.
- Stop/drain, locked membership/mode/interval and retained callback backing.
- Immediate stale-value guards, old-result rejection and new admission.
- Exact owned U64/S64/unknown-enum strings and model mutation reentrancy.

The test source does not establish native wire correctness or physical hardware
timing; those require the actual Session/backend and hardware acceptance suites.
