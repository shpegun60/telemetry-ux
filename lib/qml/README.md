# Reusable Qt/QML Configurator

`Telemetry.UX.Configurator` displays a caller-provided model and explicit actions.
It creates no client, transport, execution queue, timer, snapshot or session.
The [provider contract](Telemetry/UX/README.md) describes the required presentation
properties and methods. The existing Inspector controller is one provider; a user
can supply another QObject with that contract.

Build only the view with Qt 6.4 or newer, without an SDK installation:

```sh
cmake -S . -B /external/telemetry-ux-view -G Ninja \
  -DTELEMETRY_UX_BUILD_INSPECTOR=OFF -DTELEMETRY_UX_BUILD_CONFIGURATOR=ON \
  -DCMAKE_PREFIX_PATH=/path/to/qt -DCMAKE_INSTALL_PREFIX=/external/ux-prefix
cmake --build /external/telemetry-ux-view --target install --parallel 2
```

In a separate Qt application, add the installed prefix to `CMAKE_PREFIX_PATH`:

```cmake
find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS configurator)
target_link_libraries(my_app PRIVATE TelemetryUX::configurator Qt6::QuickControls2)
```

```qml
import Telemetry.UX

Configurator {
    anchors.fill: parent
    provider: host.deviceProvider
}
```

The module is static and directly linked. It requires no plugin import macro,
runtime environment variable, manual engine import path or Telemetry SDK provider.
Qt resource objects are part of the installed CMake target. The QML source and
`qmldir` are also installed for tooling; `TelemetryUX_QML_IMPORT_PATH` identifies
that tooling directory when needed by an IDE. The resource import works even in
an executable that loads its main QML file from disk instead of declaring
`QML_FILES` to CMake.

`NO_CACHEGEN` keeps the installed view free of Qt private QML-cache ABI dependencies.
The QML engine compiles the view normally when it loads it. This package contains
native Qt resource objects, so use a matching compiler/platform/Qt kit or rebuild
the package for that kit. The Python code-generator distribution is separate and
does not require building or installing this Qt module.

The Inspector builds this same source module when configured on its own. A root
build with Inspector enabled includes the required view even when the standalone
view option is disabled. Disabling both options performs no Qt or SDK discovery.
