# Telemetry UX

Telemetry UX contains host applications and optional UI generation for
[Telemetry SDK](https://github.com/shpegun60/telemetry-sdk). Protocols, codecs,
clients and portable transport components remain in the SDK. The UX applications reuse
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

To start from a complete application instead of assembling examples, use the
[project creator](doc/ProjectStarter.md):

```sh
python -B tools/create_project.py --target dynamic-qml --name MyDevice --output /projects/MyDevice
```

It creates an application-owned Qt Creator project with a small Model and two
independent panels. Use `--target web-device` for the complete HTTP/WebSocket
browser starter. Existing directories are never overwritten.

For an existing device with the documented TCP reference profile, create the
[connected Qt project](doc/ProjectStarter.md#qt-creator-connect-to-a-device):

```sh
python -B tools/create_project.py --target qt-connection --name MyDevice --output /projects/MyConnectedDevice
```

Open its `CMakeLists.txt` in Qt Creator with matching installed SDK/UX prefixes.
Both panels have endpoint navigation; the SDK Session owns discovery and progress.

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
transport, polling timer or FIFO. Use the installed
[Dynamic QML provider](doc/DynamicConfigurator.md) for independent panels over an
existing native Workbench owner. The Inspector uses those same installed models
and can create independent providers through `createConfigurator()`. For a known schema, use the
[generated typed QML provider](#generated-typed-qml-provider) described below.
The browser starter generates a thin entry module for the shared
web view; it does not generate another form implementation.

The pure Qt module can be built and installed without any Telemetry SDK. An
installed consumer needs neither the UX source tree nor a QML plugin or manual
engine import path. The [package test](tests/qml_package/README.md) verifies that
path after moving the install prefix and hiding the producer source/build tree.

## Generated typed QML provider

For a known device schema, the optional QML starter connects the same view to
its generated `DeviceApi` and existing `DeviceClientAdapter`:

```sh
telemetry-codegen generate Device.descriptor --output generated \
  --targets cpp,qml --structural-metadata --ux-starter qml
```

Link the generated Qt target and installed `TelemetryUX::static_configurator`.
The host supplies its existing API, client owner loop and connection:

```cpp
telemetry::ux::StaticConfiguratorProvider provider{
    device_api::qml::configuratorBinding(api)};
provider.setAllowMutations(true); // Explicit application permission.
engine.setInitialProperties({{"deviceProvider", QVariant::fromValue(&provider)}});
```

The page passes `deviceProvider` to `Configurator` and sets
`watchlistVisible: false` to hide optional polling controls. The static provider
supports Fields, Commands, Services and an explicit Individual FieldRead
Watchlist whose size follows the supplied metadata and host memory. It reads
scalar/enum Fields sequentially and does not advertise ReadMany. Its nominal
polling interval is an integer from 1 to 60,000 milliseconds; throughput depends
on the transport, device and event loop.
Generated C++ callbacks
validate values and encode requests. The provider borrows confirmed API caches,
keeps a private draft and original canonical Field bytes, and preserves untouched
NaN wire bits during edits. It creates no client, transport or Descriptor parser.
See the [static provider contract](lib/qml/Telemetry/UX/StaticConfigurator.md) for
lifetime, error and memory limits. The QML starter is opt-in; ordinary generated
SDK output is unchanged.

## Browser Configurator

Start with the [own-device application](examples/web_device_starter/README.md) to
change one small Model and run a complete connection, editing and recovery flow.
It uses the installed SDK dispatcher, shared reference HTTP/WebSocket server and
the same Configurator below. Dynamic needs no generator; Static is an explicit option.

The [browser view and providers](web/README.md) offer the same Field, Command and
Service workflow with an explicitly selected static or dynamic connection.
Install matching local `telemetry-sdk` and `telemetry-ux` npm tarballs, then pass
the SDK Session directly to the view:

```js
import {connectDynamic, demoWebSocket} from 'telemetry-sdk';
import {mountConfigurator} from 'telemetry-ux/web';

const session = await connectDynamic({transport: demoWebSocket(location.origin)});
const view = mountConfigurator(document.querySelector('#device'), {session});
// When the host is done: view.unmount(); await session.close();
```

The [Session adapter](doc/SessionProvider.md) follows lifecycle automatically.
Multiple views borrow one Session/FIFO and own separate selection and drafts;
unmounting a view leaves connection ownership with the host. Fields, Commands,
Services, Watchlist and Mixed Batch use that same Session. TRM1/TBM1 and backend
support are checked independently, with explicit Individual FieldRead fallback.
The [two-view example](examples/session_web/README.md) runs with
the existing reference server. `demoWebSocket` describes that server's bootstrap;
it is not a generic production server profile.

For advanced hosts that already own an admitted low-level client:

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
checks both real client modes in Chromium. Both modes provide Fields, Commands,
Services, bounded scalar Watchlists and explicit mixed Batch editing through the
same SDK FIFO. The host supplies discovered TRM1/TBM1 capability bytes for each
session; Individual FieldRead remains an explicit Watchlist mode.

For device-hosted files, the [deploy exporter](tools/README.md) turns a prepared
application and its installed runtime packages into a relocatable `vendor/`
bundle with MIME/length/SHA-256 manifest and an optional C++17 Flash asset table.
The target needs no Node or Python. Its HTTP/WebSocket stack and admission policy
remain application-owned; exporting files does not supply an STM32 Ethernet port.

For an existing browser WebSocket connection, the optional
[`telemetry-ux/transports/websocket` carrier](web/README.md#browser-websocket-carrier)
binds the SDK's ordinary Encoded backend. The host retains bootstrap, session,
client and socket ownership. The same demo can use a persistent localhost
WebSocket; this is a host integration example, without a firmware network server.

For low-level file inspection, the [structured payload tools](examples/structured_client/README.md)
open Descriptor, ValuesFile and response bytes and save Service request payloads.
They use the installed SDK codec and a separate localhost demonstration. The
standard Configurator remains the normal interface for connected-device work.

For declarative HTML embedding, register the standard element and attach the
same host-owned provider:

```html
<telemetry-configurator id="device"></telemetry-configurator>
<script type="module">
  import {defineConfiguratorElement} from 'telemetry-ux/web';
  defineConfiguratorElement();
  document.querySelector('#device').provider = provider;
</script>
```

The host supplies `provider`, using either static or dynamic mode. It can assign
it before or after connecting the element. Detaching the element removes only
its presentation subscription; reconnecting displays the provider's current
state. The host retains connection and client ownership. See the
[element contract](web/README.md) for replacement and lifecycle behavior.

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

The [generated QML example](examples/codegen_qml/README.md) builds its own demo
Model, exports a Descriptor and uses the installed generator and SDK. Run it
without a board in Encoded or RPC mode; no SDK test run or source checkout is
needed. Its typed `DeviceApi` and portable client adapter demonstrate generated
API use. This example does not yet supply a provider for the shared Configurator.

Keep user UI code outside generated output. Regeneration and `--check` preserve
the generator's owned-output contract. The QML target generates API and value
adapters; use the embeddable Configurator for presentation. [Source provenance](MIGRATION.md)
records the SDK revisions from which the implementations were moved.
No registry publication or new hardware qualification is implied.

[SDK + UX source audit](doc/evidence/sdk-ux-audit-2026-11-05/README.md) records source coverage, confirmed repairs and qualification limits.
