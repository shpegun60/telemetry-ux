# Installed pure Qt view qualification

This gate builds only `TelemetryUX::configurator`, with Telemetry SDK discovery
disabled. It installs and relocates the actual module, then hides both copied
producer source and build directories before compiling a separate consumer.
The native resource object files and public Qt dependencies come from the
installed export. No QML plugin, import-path environment variable, manual engine
path, application `QML_FILES` or context property supplies the view.

```sh
python3 -B tests/qml_package/run.py --build-dir /external/qml-package \
  --qt-prefix /usr --cxx /usr/bin/g++ --cmake cmake --ninja ninja
```

On Windows, explicitly choose the compiler from the matching Qt kit, native
CMake/Ninja and that Qt prefix. The runner puts the selected compiler and Qt
runtime first on `PATH`; all generated output stays in a fresh external directory.

The consumer owns an ordinary QObject provider and empty Qt models, injects it
through a required host property, and loads its main QML file from disk. Six
runtime checks prove view creation, provider identity, initial text, a notified
text update and zero QML engine warnings. Its disabled operations are explicit
refusals, without a client, timer, transport, schema parser or I/O. The real
Inspector-provider edit/write/readback workflows have separate QML tests.

A second, pure Qt provider supplies literal `<b>`/`<img>` strings through all 15
endpoint/member/enum/watchlist/status/outcome display roles and all three delegate
tooltips. The tooltips are opened through their public Popup methods. The gate
checks that text remains unchanged and uses `Text.PlainText`, while a real local
HTTP image counter stays at zero. A separate deliberately unsafe Qt `Label` with
its default AutoText must fetch exactly one image from that same counter first;
an inactive image loader therefore cannot make the regression pass. This local
test server is bounded by the consumer process timeout and always shut down. No
physical device, Telemetry client or external network service is used.

Five CMake controls check absent-Qt required/default refusal, optional component
acceptance and required/optional unknown components. A root configure with both
view and Inspector disabled must also work with Qt and SDK discovery disabled.
Export checks pin the public target, module URI/version and Qt 6.4 resource path;
resource objects must be installed, while QML private-cache dependencies and
runtime plugins must be absent. The module's license is installed as well.

`summary.json` records source and installed-prefix raw hashes before and after,
the actual tool images, public export/module metadata, binary and command logs.
Source or installed-input drift fails the gate. These are host packaging and
presentation checks, not a hermetic toolchain, performance measurement, complete
provider-operation qualification or hardware test.
