# Configurator layout regression

This test embeds the real Configurator with a disconnected SDK ConnectionSession
and QtConnectionBinding. It performs no network or device operations. The UX
QML module is built from the current source; the SDK is consumed from an explicit
installed prefix. It does not qualify protocol behavior or physical hardware.

```sh
python3 -B tests/qml_layout/run.py --build-dir /external/layout-check \
  --sdk-prefix /installed/telemetry-sdk --qt-prefix /usr \
  --cxx g++ --cmake cmake --ninja ninja
```

The real QQuickWindow embeds the Configurator in a ColumnLayout and is resized
to 1160×800 and 820×620. Collapsed and expanded
Watchlist/Batch panels must leave usable endpoint and value trees. The test checks
scroll reach, keyboard focus visibility, an actual mouse toggle, separate Batch
response/empty geometry, a reachable custom footer, and removal of optional host
slots/panels. QML warnings fail the test. Screenshots show collapsed, expanded top,
and expanded bottom states at both sizes. Source, installed SDK, tools, executable
and image hashes are retained with the result. Use a fresh external output directory.

The outer workspace scrolls vertically once the main pane's 400 px minimum and
the advanced panels exceed the viewport; inner trees/lists retain their existing
scrolling. Hiding panels or supplying host header/footer components changes only
presentation and does not change provider ownership or its operation scheduling.

The standalone 320 × 240 Watchlist also checks wrapped controls, an exact U64
value and long error status, the visible scrollbar, window-delivered synthetic wheel events, Tab focus,
and a 65-item virtualized list without issuing reads or starting polling. Widened
to 720 px, the same list becomes one 28 px table row per Field: the value aligned
under its column title, `Ok` a dot with the status word hidden, a failure in words
under the Status title and a long one cut to one line, the small remove button
inside its row (`wide-watchlist.png`); `collapsible: false` hides the expand box. Rows
can have different heights when text wraps; as with Qt ListView in general, the
scrollbar's size and position are estimates until more rows have been laid out.
