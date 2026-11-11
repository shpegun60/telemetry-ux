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
find_package(TelemetryUX 0.1.0 CONFIG REQUIRED COMPONENTS configurator)
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

For a custom page, embed `FieldEditor`, `CommandPanel` or `ServicePanel` instead
of the full Configurator. They use the same renderer and host-owned provider,
with no endpoint browser or automatic request. All four views accept optional
`headerComponent`/`footerComponent` extensions; see the provider contract above.

A host that builds its own workspace can embed the same controls individually:

```qml
import QtQuick
import QtQuick.Controls
import Telemetry.UX

SplitView {
    anchors.fill: parent
    EndpointTreePanel { provider: host.deviceProvider; SplitView.preferredWidth: 300 }
    EndpointPanel { provider: host.deviceProvider; SplitView.fillWidth: true }
    WatchlistPanel { provider: host.deviceProvider; SplitView.preferredWidth: 360 }
    BatchPanel { provider: host.deviceProvider; SplitView.preferredWidth: 360 }
}
```

The example expects a provider supporting Watchlist and Batch. Hosts that accept
other provider types must check those capabilities before instantiating the
corresponding panel, as `Configurator` does. The application owns the provider
and session and must keep them alive while any panel uses them. Sharing a
provider also shares its selected endpoint, edit draft, Watchlist and Batch; it
does not create a client, a second FIFO, or a polling timer in the view. Hiding
or showing a Watchlist panel does not stop or restart polling. If the application
wants to stop polling, it must request that explicitly via the provider. A new
provider has its own presentation and polling state and must be chosen deliberately.

`EndpointTreePanel` includes case-insensitive search by endpoint name or path
(for example, `measure.Voltage`). Results display their full path; choosing one
passes its original model index to the provider. Searching only examines the
existing model and never requests a value or changes the selected endpoint.
Clear the search to return to the tree. Search scans incrementally and displays
at most the first 500 matches, with a depth limit of 64 levels; the status says
when traversal is limited, even if there are no additional matches. Refine the
query to narrow the results.

Results contain copied display names. If an external session invalidation does
not immediately notify the view, already displayed names may remain until the
provider resets or updates its model; selection still goes through the provider's
normal validation. Hosts should notify/reset their models when their schema or
session changes.
