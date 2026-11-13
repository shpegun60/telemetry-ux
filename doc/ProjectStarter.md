# Create your own application

Create a complete application from the maintained examples, then edit your own
Model and window. This optional host tool copies a small allowlist of application
templates; it does not generate DTOs or introduce another code generator.
Both Qt targets require CMake 3.25+, Ninja and a matching C++20 Qt 6.12+ kit.

## Qt Creator: connect to a device

```sh
python -B tools/create_project.py --target qt-connection --name MyDevice --output /projects/MyConnectedDevice \
  --sdk-prefix /installed/sdk --ux-prefix /installed/ux --qt-prefix /installed/qt
cd /projects/MyConnectedDevice
cmake --preset local
cmake --build --preset local
```

Open `CMakeLists.txt` in Qt Creator with the matching Qt 6.12+ C++20 kit, or run
`telemetry-qt-connection` (`.exe` on Windows) from the build directory. SDK and UX
must include their optional `qt_connection` components; UX is configured with
`TELEMETRY_UX_BUILD_QT_CONNECTION=ON`. Omitting the three prefix arguments creates
the same editable project without a machine-specific preset.

Enter the matching device's host/port and press Connect. For an existing local
Inspector installation, `telemetry-inspector-qml --serve-demo 5577` starts its
read-only reference device. The creator does not install a server or firmware.

The three copied public application files create one `ConnectionSession`, one
`QtConnectionBinding` and two independent providers. The SDK owns connection,
Descriptor indexing, progress, native buffers and the one FIFO; your application
composes two ready Configurators with independent navigation in `Main.qml`. Another panel
requires another `presentation.createProvider()`, not another client or pump.
No generated DTO or private Inspector source is copied.

This target selects the documented exclusive Resource-bootstrap/Encoded TCP
reference profile. It does not connect to arbitrary TCP or infer authentication
or remote recovery from an address. Use a matching trusted server; transport
security and firmware networking are application responsibilities. A fresh full
bootstrap from the same peer proves supersession only under that server contract.
Disconnect preserves terminal operation facts, reconnect is explicit, and no
uncertain mutation is replayed. Providers can be removed independently of their
shared connection. See the generated README and the
[owned connection example](../examples/qt_connection/README.md) for lifecycle and
explicit host/panel mutation policy.

## Qt Creator: local Model

```sh
python -B tools/create_project.py --target dynamic-qml --name MyDevice --output /projects/MyDevice
```

Open the new `CMakeLists.txt` in Qt Creator and select your matching C++20 Qt 6.12+
kit. Set `CMAKE_PREFIX_PATH` to installed matching Telemetry SDK, UX and Qt
prefixes. The SDK needs `core`, `workbench`, `encoded_backend`, `encoded_server`;
install UX with `TELEMETRY_UX_BUILD_DYNAMIC_CONFIGURATOR=ON`.

To record those local paths in a ready CMake preset, supply them at creation:

```sh
python -B tools/create_project.py --target dynamic-qml --name MyDevice --output /projects/MyDevice \
  --sdk-prefix /installed/sdk --ux-prefix /installed/ux --qt-prefix /installed/qt
cd /projects/MyDevice
cmake --preset local
cmake --build --preset local
```

Run `telemetry_dynamic_example` (`.exe` on Windows). It shows two independent
panels over one local native Encoded owner and FIFO. Refresh, opt in to changes,
Edit and Apply. The value comes from separate actual readback; the sibling must
Refresh explicitly. Change `Device.hpp` and `Main.qml` for your application.
For an external connection retain your existing transport/Workbench owner;
panels do not open another connection. The generated README and lifecycle
comments explain the owner loop and retirement boundary.

## Browser application

```sh
python -B tools/create_project.py --target web-device --name MyDevice --output /projects/MyWebDevice \
  --sdk-prefix /installed/sdk --sdk-package /packages/telemetry-sdk-0.1.0.tgz \
  --ux-package /packages/telemetry-ux-0.1.0.tgz
cd /projects/MyWebDevice
python -B prepare.py
python -B serve.py
```

The first command creates application sources only. Preparation needs the usual
C++20, CMake 3.21+, Ninja, Node/npm and Python tools; the SDK prefix needs `core`,
`encoded_server`, `batch_server`. Run `serve.py` with a Python environment that
contains `websockets==15.0.1`. Its localhost address is printed at startup.
The generated README gives tool overrides and an optional installed code-generator
interpreter for Static mode. Dynamic mode requires no generator.

Change `examples/web_device_starter/native/Device.hpp` for your Model and its
`index.html` / `DeviceApp.js` for the page. Build/package/generated output goes
to a fresh sibling `<project-directory>-prepared`, separately from your sources.
The supplied browser drives FieldRead, normalized Write/readback, Command and
Service through one Session. Explicit reconnect never replays an uncertain
operation. The shared HTTP/WebSocket reference is copied locally; the generated
project no longer needs the producer checkout.

This is a localhost host starter, not an STM32 HTTP/WebSocket implementation or
a production access policy. Adapting a real server requires its own connection,
session/bootstrap and remote-retirement contract. No universal transport or
exactly-once execution guarantee is inferred from a URL.

## Output and input contract

`--output` must not exist, including an empty directory. Parent traversal,
symlink/reparse ancestors, case aliases, ambiguous path names, invalid project
names and overlapping producer/installed paths are refused before creation.
Names are bounded ASCII identifiers, not CMake or HTML fragments. If local target
paths are supplied, provide the whole matching set; otherwise the generated
README explains how to supply them later. Package identities and required CMake
package files are checked before the destination is created; the subsequent
consumer build validates actual toolchain/ABI compatibility. Matching development
version labels alone are not a substitute for an accepted SDK/UX source pair.

The tool never deletes or overwrites existing application files. It prepares all
destination bytes before creating directories. An unexpected filesystem write
failure may leave an incomplete fresh directory for inspection; it never removes
that directory automatically. Use a filesystem whose paths remain under your
control during creation.

`project-manifest.json` records template hashes and initial file hashes. Repeating
the same target/name/options produces identical project bytes. Your application
directory belongs to you: upgrades create another fresh project for comparison,
and generated DTO output remains separate. Installed packages and build/runtime
dependencies are still required; no SDK source or private Inspector file is copied.

[Project qualification](../tests/project_starter/README.md) builds the generated
applications after hiding their producer templates, executes the local Model
panels, builds the connected-device consumer from installed packages, and
exercises the real browser journey rather than only comparing copied files.
