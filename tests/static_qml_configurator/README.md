# Installed static QML Configurator

This offline gate starts with the maintained
[generated binding/cache gate](../codegen_qml/configurator/README.md), then builds
the real shared provider and `Telemetry.UX` view against the same generated SDK.
The application-owned Model, generated `DeviceApi`, existing `DeviceClientAdapter`
and one Encoded backend/FIFO execute the Field, Command and Service workflows.
No SDK source-tree fallback, device connection or hardware operation is used.

The real keyboard/mouse edit test runs through both Configurator and standalone
FieldEditor. Standalone CommandPanel and ServicePanel reject mismatched
selections and submit only after an explicit click, through the same generated
API/FIFO. Mounting them sends no packet. The GUI floor is 494 checks, including
exact U64/S64, lifecycle and permission regressions.

```sh
python3 -B tests/static_qml_configurator/run.py \
  --sdk-prefix /external/installed-sdk \
  --ux-prefix /external/installed-ux \
  --codegen-python /external/matching-codegen-venv/bin/python \
  --qt-prefix /usr --cxx g++ --build-dir /external/static-qml-gcc
```

Both prefixes and matching Python distributions must already be installed at
version 0.1.0. Use a fresh output directory; the runner never overwrites earlier
evidence. For Clang 18, add `--sanitize --cxx clang++-18` and supply fully
ASan/UBSan-instrumented SDK and UX prefixes. The runner checks the actual installed
archive symbol tables, uses `detect_leaks=1`, and retains the sanitizer settings.
It sets offscreen/software Qt rendering, so no visible desktop window is opened.
On Windows it selects the Basic controls style and the existing Windows Fonts
directory: native theme handles are unavailable on the offscreen platform.
The strict QML diagnostic check remains enabled; no warning is filtered.

Qt 6.4.2 on Linux has a separately reproduced software scenegraph texture-cache
leak, also present in a Qt-only Window/Controls program with no Telemetry code.
For that exact runtime, backend and sanitized GUI profile, the runner first
executes the full journey without suppression. A clean exit 0, completed
assertions and no sanitizer diagnostics are accepted directly; the extracted
panels can avoid retaining the Qt texture, so a leak is not a required result.
Only exit 86 with the proved Qt texture allocation enables the sole
`QSGRenderContext::textureForFactory` LSan rule for a second normal GUI run.
Every sanitized profile also runs the same journey with an intentional 123-byte
application leak: that named allocation must still fail with exit 86 and an
exact 123-byte leak summary. No other exit is accepted. Ambient `LSAN_OPTIONS`
are removed.
The nested generated gate and `Contracts.cpp` remain unfiltered, and other Qt
versions receive no exception. This is a bounded external Qt qualification
exception, not an unconditional leak-free GUI claim.

The [Qt 6.4.2 base context](https://raw.githubusercontent.com/qt/qtdeclarative/v6.4.2/src/quick/scenegraph/qsgcontext.cpp)
retains texture pointers while its destructor does not delete them;
[software invalidation](https://raw.githubusercontent.com/qt/qtdeclarative/v6.4.2/src/quick/scenegraph/adaptations/software/qsgsoftwarecontext.cpp)
also leaves the texture collections untouched. The corresponding
[Qt 6.8.0 implementation](https://raw.githubusercontent.com/qt/qtdeclarative/v6.8.0/src/quick/scenegraph/adaptations/software/qsgsoftwarecontext.cpp)
explicitly destroys both collections. This comparison does not establish the
first fixed Qt release. The receipt records the actual runtime version, rule
hash when applied, primary sources, per-command sanitizer settings and outcomes.
The receipt distinguishes an eligible Qt profile from an actually applied rule.

The native GUI executable checks confirmed Read → Edit → Apply → separate
Readback, exact large integers/open enums, floating wire-bit preservation,
permission opt-in, request initialization, routing/application outcomes and
session/error handling. Real QTest keyboard input followed by a mouse Apply
without Enter uses the shared Configurator. Static Watchlist exercises three
real scalar Fields through Individual FieldRead, including exact U64/S64,
unknown enum, Stop/drain and session invalidation. Its mode selector does not
offer ReadMany. `window.png` records the actual rendered window for visual review.

`Contracts.cpp` uses an explicitly fake application API facade to isolate Qt
presentation contracts. Its checks cover lazy 128-row fetching, stale and foreign
indices, draft/seed retention after refusal, lifecycle and callback reentry,
presentation budgets, queued notification coalescing and supported destruction
boundaries. Those checks are not reported as additional SDK wire transactions.

The summary keeps both scopes separate. It records executed binary hashes,
generated SDK and command-log hashes, all candidate UI/generator/test/fixture
sources, both installed prefixes, installed generator ownership and tool
identities before and after execution. Competing SDK and UX roots are supplied
while explicit package directories are asserted, and compiled sources must
belong to UX or the external generated/build directory. A changed input, failed
assertion, Qt warning or unexpected sanitizer result makes the gate fail.

The current qualification floors are 340 generated-seam checks, 36 compiled
naming endpoints, three generator unit tests with 14 sealed default-output
hashes, 494 native/provider/QML checks with zero warnings, and 297 isolated
presentation-contract checks. Each individual assertion remains authoritative;
the floors also reject accidentally skipped test paths.
