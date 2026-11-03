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
