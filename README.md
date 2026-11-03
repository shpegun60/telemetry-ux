# Telemetry UX

Telemetry UX contains host applications and optional UI generation for
[Telemetry SDK](https://github.com/shpegun60/telemetry-sdk). Protocols, codecs,
clients and transport components remain in the SDK. The UX applications reuse
one existing client/FIFO per session.

## Universal Inspector

The [Inspector](apps/inspector/README.md) discovers an unknown device, reads
Fields, edits configuration, executes Commands, calls Services and polls a
scalar Watchlist. Its existing Demo and loopback TCP modes work without a board.
The UART mode uses the SDK's optional SerialPort transport.

Install a matching Qt kit and SDK prefix, then configure, build and launch:

```sh
cmake -S apps/inspector -B /external/inspector -G Ninja \
  -DCMAKE_PREFIX_PATH="/path/to/sdk-prefix;/path/to/qt-prefix"
cmake --build /external/inspector --parallel 2
/external/inspector/telemetry-inspector-qml
```

For UART, add `-DINSPECTOR_ENABLE_SERIAL=ON` and use an SDK prefix that contains
`qt_serial_transport`. The application build needs no COBS checkout, SDK source
tree, generated per-device DTO or Python installation. The application README
lists the SDK components to install. The root CMake project builds the same
application; it does not introduce another implementation.

## Embed the Configurator in your application

Use the same [Configurator view](lib/qml/README.md) inside your own Qt Quick
window or tab. Install the view package, link `TelemetryUX::configurator` and
supply a QObject provider:

```qml
import Telemetry.UX

Configurator {
    anchors.fill: parent
    provider: host.deviceProvider
}
```

The [provider contract](lib/qml/Telemetry/UX/README.md) keeps connection controls,
permission opt-in and device ownership in the host. The view creates no client,
transport, polling timer or FIFO. The existing Inspector controller supplies the
dynamic provider. A generated typed provider and generated web forms are later
steps; the current generator does not create either of them.

The pure Qt module can be built and installed without any Telemetry SDK. An
installed consumer needs neither the UX source tree nor a QML plugin or manual
engine import path. The [package test](tests/qml_package/README.md) verifies that
path after moving the install prefix and hiding the producer source/build tree.

## Optional generator

The generator extension depends on the exact matching SDK code-generator
version and provides no second parser or command-line entrypoint.

The `telemetry-ux-codegen` development package adds `--targets qml` to the
SDK-owned `telemetry-codegen` command. Its Python runtime needs no Qt dependency;
building the generated QML module still requires the documented matching Qt kit.
Headless C++/JavaScript/TypeScript generation needs only the SDK core package.

Install local matching wheels into a fresh environment:

```sh
python3 -m venv /external/codegen-env
/external/codegen-env/bin/python -m pip install --no-index \
  --find-links /external/packages telemetry-codegen-core==0.1.0 \
  telemetry-ux-codegen==0.1.0
/external/codegen-env/bin/telemetry-codegen generate descriptor.bin \
  --targets qml --output /external/generated
```

The module-free `telemetry-codegen==0.1.0` compatibility package installs both
packages. For an old monolithic installation, use a fresh environment or
explicitly uninstall the old `telemetry-codegen` package before installing the
new packages. An in-place upgrade across overlapping module owners is not
supported. The existing `python -m tools.telemetry_codegen` invocation remains
owned by the core package.

Keep user UI code outside generated output. Regeneration and `--check` preserve
the generator's owned-output contract. The current QML target generates API and
value adapters, not automatic forms. Generated web starters have separate
implementation and usability gates. [Source provenance](MIGRATION.md)
records the SDK revisions from which the implementations were moved.
No registry publication or new hardware qualification is implied.
