# Two Dynamic QML panels in your application

Open this `CMakeLists.txt` in Qt Creator with a Qt 6.12+ C++20 kit. Set
`CMAKE_PREFIX_PATH` to your installed matching SDK and UX prefixes. The example
uses only installed packages; it does not include Inspector application files or
require generated DTOs, Python, a physical board or a network server.

```sh
cmake -S examples/dynamic_qml -B /external/my-dynamic-window -G Ninja \
  -DCMAKE_PREFIX_PATH="/external/sdk-prefix;/external/ux-prefix;/path/to/Qt"
cmake --build /external/my-dynamic-window --parallel 2
/external/my-dynamic-window/telemetry_dynamic_example
```

The SDK prefix needs `core`, `workbench`, `encoded_backend` and `encoded_server`.
Build/install UX with `TELEMETRY_UX_BUILD_DYNAMIC_CONFIGURATOR=ON`. Use a fresh
build directory when changing compiler or Qt kit. On Windows use native paths
and the executable's `.exe` suffix.

There are only three presentation-connection lines in `main.cpp`:

```cpp
auto connection = std::make_shared<DeviceConnection>(); // Your existing owner.
telemetry::ux::DynamicConfiguratorBinding binding{connection->workbench, connection};
telemetry::ux::DynamicConfiguratorProvider configProvider{&binding}, valueProvider{&binding};
```

The binding borrows the existing admitted Workbench and retains its backing
storage. Each provider obtains independent selection, draft, snapshot and Service
response while using that Workbench's one DynamicClient/FIFO. The shared lifetime
token must really retain the Workbench, Descriptor and native buffers.

QML is presentation only:

```qml
import QtQuick
import Telemetry.UX

Row {
    Configurator { width: 700; height: 800; provider: configProvider }
    FieldEditor { width: 500; height: 800; provider: valueProvider }
}
```

Change the DTOs and tables in `Device.hpp`. Both panels discover the resulting
schema without another UI class. Both initially select Config and start read-only.
Press Refresh, then enable the corresponding **Allow changes** checkbox before
Edit/Apply or Command/Service submission. Each checkbox controls only its own
panel; the host must also permit mutations. Each panel has its own
Refresh/Edit/Apply state. A managed mutation invalidates the other panel's cached
data; press Refresh explicitly to obtain the actual device value. Host policy and
the panel's user opt-in both control mutation permission.

`DeviceConnection::process()` is a small, real, local Encoded loop so this project
runs immediately. In your host, keep your existing asynchronous transport and
owner loop; call `binding.poll()` after progressing that owner. Do not copy this
local dispatcher into a network transport or create another client for a panel.
The provider performs no discovery, connection, reconnection or transport pumping.

Before replacing/destroying the connection: retire the binding, drain accepted
native work using the owner's normal lifecycle, call `poll()` to deliver operation
facts, then destroy it only when `drained()`. Removing a provider merely detaches
its panel; `binding.operationFinished` retains checked outcomes even for removed
panels. Physical TX release and remote-session recovery remain owner responsibilities.

`--smoke` creates both actual QML panels, submits two reads through the one FIFO,
checks both snapshots and exits. The installed
[Dynamic-provider test](../../tests/dynamic_qml_configurator/README.md) covers
real checkbox/button/keyboard editing, normalized Write/Refresh, placeholder
geometry, longer lifetime/error/reconnect scenarios and sanitizer qualification.
