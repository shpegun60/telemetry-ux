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
dynamic provider. A generated typed QML provider remains a later step. The
browser starter described below generates a thin entry module for the shared
web view; it does not generate another form implementation.

The pure Qt module can be built and installed without any Telemetry SDK. An
installed consumer needs neither the UX source tree nor a QML plugin or manual
engine import path. The [package test](tests/qml_package/README.md) verifies that
path after moving the install prefix and hiding the producer source/build tree.

## Browser Configurator

The [browser view and providers](web/README.md) offer the same Field, Command and
Service workflow with an explicitly selected static or dynamic client. Install
the matching local `telemetry-sdk` and `telemetry-ux` npm tarballs in your host
application, then supply its existing admitted client:

```js
import {createDynamicProvider, mountConfigurator} from 'telemetry-ux/web';

const provider = createDynamicProvider({client});
const view = mountConfigurator(document.querySelector('#device'), {provider});
```

Use `createStaticProvider({client, metadata: structuralMetadata, sdk})` for the
generated SDK. Both modes share one renderer. It builds request forms from the
schema, keeps 64-bit inputs as decimal text, preserves invalid/intermediate input
and reads back the device's applied configuration. Changes require explicit
opt-in; `Accepted`, `Executed`, endpoint refusal and `OutcomeUnknown` stay distinct.
Editing a Field keeps its original wire payload privately so unchanged F32/F64
NaNs retain their sign, signaling bit and payload when another member is edited.

The host forwards lifecycle changes with `provider.invalidate()` before closing
or resetting its client, and `provider.sync()` after a new admission. Removing a
view with `view.unmount()` keeps client ownership with the host. Applications can
load `telemetry-ux/configurator.css` and resolve package imports through their
existing bundler or import map. These packages are development artifacts; no
registry publication is implied.

The [local browser demo](examples/web_demo/README.md) uses a persistent native
Model and the existing Encoded dispatcher. Its [installed consumer test](tests/web_configurator/README.md)
checks both real client modes in Chromium. This first browser version includes
Fields, Commands and Services; the Qt Inspector also provides Watchlist controls.

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

For a static browser page, opt in to a thin entry module alongside the JavaScript
SDK and structural metadata:

```sh
/external/codegen-env/bin/telemetry-codegen generate descriptor.bin --targets js --structural-metadata \
  --ux-starter web --output /external/generated
```

Import `mountDevice` from the generated `ui/DevicePage.js` and pass the host's
admitted `DeviceClient`. It returns `{provider, unmount}` and uses the same shared
browser view. The host supplies connection controls and its chosen transport.
The starter enables the SDK's optional `--wire-editing` codecs. When generating
a static SDK without the starter, select that option explicitly for Field editing;
older read-only generated codecs remain usable for inspection.
The [starter test](tests/web_starter/README.md) checks this ownership wiring.

Keep user UI code outside generated output. Regeneration and `--check` preserve
the generator's owned-output contract. The QML target generates API and value
adapters; use the embeddable Configurator for presentation. [Source provenance](MIGRATION.md)
records the SDK revisions from which the implementations were moved.
No registry publication or new hardware qualification is implied.
