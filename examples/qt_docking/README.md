# Qt Quick docking example

A desktop window built from the installed Telemetry SDK and Telemetry UX panels,
arranged in movable docks with [KDDockWidgets](https://github.com/KDAB/KDDockWidgets).
It shows how an application composes the ready panels; it is not part of the
library, and the library does not depend on KDDockWidgets.

What it demonstrates:

- **One connection, two presentations.** One SDK `ConnectionSession` and one
  `QtConnectionBinding` own the device connection and its request queue. Two
  providers from `binding.createProvider()` drive independent panels — the main
  tree, endpoint and Watchlist, and a separate **Compare endpoint** pair — each
  with its own selection, sharing the same session.
- **Docks you can rearrange.** Device tree, Selected endpoint, Watchlist, Batch,
  Trend, Compare endpoint, Connection and an example placeholder for your own QML.
  They can be tabbed, floated, closed and reopened from **Panels**; **Panels →
  Reset layout** returns to the default.
- **A remembered layout.** The layout is saved on a normal close and restored at
  the next start. Connection is hidden in a new or reset layout, and restored open
  if you left it open. Closing the Connection dock does not disconnect. A damaged
  or missing layout file falls back to the default.
- **Trend** of one watched Field, from readings the Watchlist already has; it
  never starts or stops polling.

The example is read-only: it never connects by itself, and writes, Commands and
Services stay disabled.

## Licence: read before you copy it

KDDockWidgets is licensed by KDAB under **GPL-2.0-only or GPL-3.0-only**, or under
a commercial licence. Telemetry SDK and UX, and this example's own source, are
MIT. Building and using it inside your organisation is not restricted by the
GPL; **distributing** a program that includes KDDockWidgets to others requires
either distributing that program under the GPL or a commercial licence from KDAB.
This is a summary, not legal advice; read the
[KDDockWidgets licence](https://github.com/KDAB/KDDockWidgets/blob/674f28453219dddedafd4b8f33cf094e8e546be3/LICENSE.txt)
for your case. For that reason the example is not built by the default library
build, by CI or by packaging, and nothing downloads KDDockWidgets for you.

## Build

You need a Qt 6.4+ desktop kit, the Telemetry SDK installed with the
`qt_connection` component, Telemetry UX installed with
`TELEMETRY_UX_BUILD_QT_CONNECTION=ON` against that SDK, and KDDockWidgets.

### 1. KDDockWidgets

The example was checked with KDDockWidgets **v2.5.1** (upstream commit
`674f28453219dddedafd4b8f33cf094e8e546be3`), Qt 6.10.1 and MinGW 13.1. Its Qt
Quick frontend uses Qt private headers, so build it **with the same Qt version and
compiler** you build this example with:

```powershell
cmake -S <KDDockWidgets-source> -B <KDDockWidgets-build> -G Ninja `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/mingw_64 `
  -DKDDockWidgets_FRONTENDS=qtquick -DKDDockWidgets_QML_MODULE=ON `
  -DKDDockWidgets_EXAMPLES=OFF -DKDDockWidgets_TESTS=OFF
cmake --build <KDDockWidgets-build> --parallel 4
```

That build directory contains the QML module (`com/kdab/dockwidgets/qmldir`) and,
on Windows, the library in `bin/`.

### 2. This example

```powershell
cmake -S . -B <example-build> -G Ninja `
  '-DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/mingw_64;<sdk-prefix>;<ux-prefix>' `
  -DKDDOCKWIDGETS_QML_IMPORT_DIR=<KDDockWidgets-build> `
  -DKDDOCKWIDGETS_BIN_DIR=<KDDockWidgets-build>/bin
cmake --build <example-build>
<example-build>/telemetry-qt-docking
```

`KDDOCKWIDGETS_QML_IMPORT_DIR` is added to the QML import path and
`KDDOCKWIDGETS_BIN_DIR` is put first on `PATH` at start, so the program runs
without further setup. Without them the example still builds (CMake warns), and
needs `QML_IMPORT_PATH` and `PATH` set at run time instead. If the module cannot
be found, the program says so and exits with code 2:

```
This example needs the KDDockWidgets 2.5 Qt Quick QML module (com.kdab.dockwidgets),
built for this exact Qt version ...
```

A directory without the module stops the configuration with an error.

In Qt Creator, open this `CMakeLists.txt` with the kit KDDockWidgets was built
with and add the two variables above to the CMake configuration.

### 3. A device to talk to

Start the localhost device emulator (`telemetry-device-emulator`, or the UX
Inspector's demo server), open **Connection…**, keep `127.0.0.1:5577` or type the
emulator's port, and press **Connect**.

## Checks

`tests/check_layout.py` runs the built example several times against layout
files in a fresh temporary directory — never your settings — and never
connects to a device:

```powershell
python tests/check_layout.py --exe <example-build>/telemetry-qt-docking.exe
python tests/check_layout.py --exe <example-build>/telemetry-qt-docking.exe --platform offscreen
```

| Check | What must happen |
| --- | --- |
| `--smoke-test` | Open Connection, float and dock a panel, reset to the default |
| `--prepare-layout` | Open Connection and Compare, float Device tree and Trend, save |
| `--expect-layout restored` after that | Every one of those states comes back |
| control: no saved file, expect restored | Must **fail** — restoring nothing cannot pass |
| control: the prepared file, expect default | Must **fail** — the prepared layout differs from the default |
| no saved file, expect default | The default layout |
| a damaged file, expect default | Falls back to the default |
| `--title-bar-check` | The Device tree's title-bar float button floats it, and the same button in its floating window docks it back — real mouse press and release events on those buttons |

On Windows with Qt 6.10.1, MinGW 13.1 and KDDockWidgets 2.5.1, all nine pass with
native windows and offscreen, with zero QML warnings.

**Not covered by these checks:** dragging a dock by its title bar to another
place (a drag follows the global cursor, which synthesized events do not move —
it was checked by hand once), several monitors, and DPI changes. The two-process
check with the emulator (reading values through two providers) is still in the
`telemetry-client/experiments/kddock_workspace` prototype and is to move here
before that prototype is removed.
