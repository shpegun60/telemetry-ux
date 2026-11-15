# Independent installed Dynamic QML providers

This downstream consumer uses only installed `TelemetryUX::dynamic_configurator`
and SDK targets. It does not compile `examples/inspector`, import application-private
headers, generate another SDK or create one client per panel.

Prepare matching installed SDK (`core`, `workbench`, `encoded_server`,
`encoded_backend`) and UX (`dynamic_configurator` enabled) prefixes. Then:

```sh
python -B tests/dynamic_qml_configurator/run.py --build-dir /external/dynamic-provider-check \
  --sdk-prefix /external/sdk-prefix --ux-prefix /external/ux-prefix \
  --qt-prefix /path/to/Qt --cxx g++ --cmake cmake --ninja ninja
```

For the Linux Clang sanitizer profile build and install **both** packages with
ASan/UBSan first, then add `--sanitize --cxx clang++-18`. The runner checks the
actual installed core, dispatcher and provider archives for instrumentation.
It records exact source/prefix/tool/executable/log hashes and rejects source drift
or reuse of nonempty output. All generated files remain outside the checkouts.
The same runner independently configures and builds the installed-only
[two-panel application](../../examples/dynamic_qml/README.md), then runs its real
QML smoke journey. It requires at least 787 provider checks and no QML warnings.

The C++ fixture uses the real Model, Descriptor, RuntimeSchema, Workbench owner,
EncodedBackend and Encoded dispatcher with a deterministic owner clock. Coverage:

- Independent selections, exact drafts/snapshots and globally ordered requests
  over one owner FIFO; `QueueFull` preserves draft and confirmed state.
- Three real QML panels, parallel accepted work, middle panel removed during a
  Service, retained owner operation fact and remaining panels continuing normally.
- A provider without Watchlist support creates no Watchlist pane, even if its
  Configurator's `watchlistVisible` is explicitly enabled.
- The actual example `Main.qml`, with per-panel opt-in checked by real mouse
  input, Refresh/Edit/Apply controls and typed input without Enter. Server Write
  and separate normalized readback are checked, as are sibling invalidation and
  empty-value/response labels fitting their viewports. At 1400 x 820 the
  configuration panel must retain at least 800 pixels of width and the field
  panel 480-520 pixels; actual input controls must fit their own panel and window.
- Session retirement, checked quiescence, fresh admission with the same endpoint
  IDs, stale QModelIndex refusal, no implicit refresh or mutation replay.
- Provider/target-binding deletion during model reset, provider deletion from a
  completed signal, owned Service response surviving RX/server overwrite.
- Exact U64/S64, unknown enum, six positive/negative qNaN/sNaN/signed-zero pairs,
  normalized readback and bounded 128-row lazy array materialization.
- Post-handoff lost Command result remains `OutcomeUnknown` at the owner even
  when its presentation is removed; late old replies are rejected.
- `DynamicConfiguratorBinding::submitted()` once per accepted Refresh, Apply,
  Command and Service, with the request already in the owner's queue (an
  Apply's read back is the Workbench's own and adds none), and never for a
  refusal - what a host whose owner runs only on demand wakes it on.
- The central Undo's history (UR-1) on a target of the check's own: a step
  recorded once its change is made, undo and redo putting states back, steps
  of one merge key within two seconds merging (and going when back where they
  started), a merge window of the caller's own and 0 (merged while the key
  stays the same), a blocked step keeping its words (an undo's and a redo's),
  a skipped step and a dead target's step going, the limit, the irreversible
  note ended by a step recorded or taken, a notice's `markNoticed`, the kinds
  `done` names, `clean`, a kind not
  declared or a target that is not one refused with a warning, a host's own
  kind declared; a provider's `history` assigned and announced once.
- The live Plot's steps (UR-1) on a provider's own traces, every `plot.*` kind
  held to `tests/helpers/UndoKinds.hpp` - done, undone and redone the same,
  across the provider losing its device and getting it back and across the
  Watchlist naming other paths; a curve back in its place with its colour;
  Remove all as one step; a curve's points merged within two seconds; the
  words; `restore()` recording nothing; a curve gone meanwhile skipped.

QML warnings remain failures. The sanitizer profile uses only the existing narrow
upstream Qt software-texture exception (`QSGRenderContext::textureForFactory`);
an independent application-leak process must still be rejected. See the existing
[static-provider gate](../static_qml_configurator/README.md) for its Qt source
provenance. No board, Ethernet, UART or DMA qualification is implied.
