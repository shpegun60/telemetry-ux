# Export your own device's browser application

The own-device starter can prepare a self-contained browser tree and an optional
C++17 Flash asset table in the same command that builds its small native Model.
Dynamic mode needs no generated SDK. The browser keeps the existing Session,
Configurator, Watchlist and Batch; there is no second page implementation or FIFO.

## Create, prepare and try it

Create an application-owned project with matching installed SDK and npm packages:

```sh
python -B tools/create_project.py --target web-device --name MyDevice \
  --output /projects/MyDevice --sdk-prefix /installed/sdk \
  --sdk-package /packages/telemetry-sdk-0.1.0.tgz \
  --ux-package /packages/telemetry-ux-0.1.0.tgz
cd /projects/MyDevice
python -B prepare.py --export-web cpp
python -B serve.py --exported
```

Preparation needs the [own-device starter's development tools](../examples/web_device_starter/README.md#prepare-and-run).
Run `serve.py` with a Python environment containing `websockets==15.0.1`.
The generated project includes the maintained exporter; neither preparation nor
serving needs the original UX checkout. Change
`examples/web_device_starter/native/Device.hpp` for your Model and rebuild into a
fresh preparation output. On Windows use normal absolute paths and the Python
environment's `Scripts/python.exe`.

The default preparation directory is the project's sibling
`MyDevice-prepared`. It contains:

| Path | Purpose |
| --- | --- |
| `native/` | Native Model host used by the reference test server |
| `public/` | Development page and installed npm runtime packages |
| `www/` | Relocatable deploy files and local `vendor/` runtime closure |
| `www/assets.json` | URL path, MIME type, length and SHA-256 for every runtime asset |
| `www/telemetry_web_assets.hpp` | Optional immutable C++17 table with `--export-web cpp` |
| `prepared.json` | Preparation status, package hashes and deploy manifest/header hashes |

The same feature is available directly from the checkout:

```sh
python -B examples/web_device_starter/prepare.py --output /external/my-device \
  --sdk-prefix /installed/sdk --sdk-package /packages/telemetry-sdk-0.1.0.tgz \
  --ux-package /packages/telemetry-ux-0.1.0.tgz --export-web files
python -B examples/web_device_starter/serve.py --prepared /external/my-device --exported
```

`--export-web files` emits the runtime tree and manifest. `--export-web cpp` adds
the header for a firmware HTTP stack that serves compiled-in assets. Omitting
the option preserves ordinary development preparation and does not create `www/`.
`serve.py --exported` verifies the preparation receipt, manifest and every listed
asset's regular local path, length and SHA-256 before opening its host server.
Changed or missing assets produce an error before any native connection is
started. Ordinary `serve.py` continues to use `public/`.

The browser journey remains Connect, select Config, Read, opt in to changes,
Edit and Apply + readback. The native Model normalizes `periodMs` from 1 to 10;
only the separate readback publishes the applied value. Commands, Services and
polling use the same connection. Disconnect/reconnect never replay a mutation.

## Static is an explicit option

Add `--codegen-python /path/to/python-with-telemetry-codegen-core` to prepare
Static alongside Dynamic mode. Preparation creates one small
`static-context.js` module for the selected build. With Static enabled it loads
the generated SDK and structural metadata. Without Static it has no generated
module imports and reports an explicit error if Static is selected by application
code. There is no fallback from an unavailable or mismatching Static SDK to Dynamic.

Both modes use the same maintained `DeviceApp.js` and view. Application files,
including the local favicon, are explicitly exported. The existing exporter
keeps its local-reference checks; conditional missing imports and inline asset
URLs are not silently ignored.

## Set the target's budgets

```sh
python -B prepare.py --export-web cpp --web-max-bytes 524288 --web-max-files 64
```

Defaults match the exporter: 4 MiB runtime bytes and 256 files. Byte budgets must
be positive; file budgets range from 1 through 4096. These options require
`--export-web`. The runtime total includes application assets, copied runtime
modules and license files after rewriting. The manifest and C++ source text
are build artifacts outside that runtime-byte total. The header expands bytes
into C++ source; its file size is recorded separately in `prepared.json`.

The `web_export` receipt records `mode`, relative `directory` and `manifest`,
`manifest_sha256`, `total_bytes` and `asset_count`. In C++ mode it also records
the relative `cpp_header`, `cpp_header_sha256` and `cpp_header_bytes`.
Require `passed: true` before using a preparation output. An export preflight
failure creates no `www/`; an I/O error can leave an incomplete new directory,
which must not be treated as ready. Existing output is never replaced. Use a
fresh preparation directory for the next attempt.

## Include your own files

Place additional browser files beside the starter source or in one of its
subdirectories. In a created project, that source directory is
`examples/web_device_starter/`. Select each file explicitly:

```sh
python -B prepare.py --export-web cpp \
  --web-asset hints/presentation.json --web-asset images/logo.svg
```

The names are relative to the starter **source**, not a generated `public/` or
`www/` directory. Use forward slashes. Selected bytes are copied to `public/`
and exported to `www/` under the same relative names. Include ordinary local
CSS, images or a [presentation document](PresentationMetadata.md) this way.
The application still chooses whether and how to load them; selecting a JSON
file neither applies its hints nor grants permission to write a device.

Names and regular files are checked with the existing exporter's rules before
preparation creates output or invokes the native build. Missing files, remote
or absolute paths, traversal, links, duplicate/case aliases, collisions with
managed app/vendor/generated files and oversized extra inputs are refused.
All selected files count toward the same runtime byte/file budgets; the full
runtime closure is checked again during export. No source file is overwritten.

## Serve the assets from your firmware

For a filesystem, deploy the `www/` runtime paths listed by the manifest. Do not
copy `public/node_modules`, development tools or the native host executable to
the device. For compiled-in assets, include the generated header:

```cpp
#include "telemetry_web_assets.hpp"

const auto* asset = telemetry_ux_assets::find("/index.html");
if (asset) {
    // Your HTTP stack sends asset->data, asset->size and asset->mime.
}
```

Map your HTTP root `/` to `/index.html` and serve exact exported paths. The table
borrows immutable bytes and adds no heap allocation, HTTP parser, task or queue.
Its data counts against the firmware's Flash budget. Your linker and target's
memory layout determine whether it fits; the 4 MiB default is not a board claim.
Node, npm and Python are development tools and are not needed to serve these bytes.

This output supplies assets, not a firmware HTTP/WebSocket server. Keep the
existing [server and lifecycle contract](../examples/web_device_starter/README.md#exact-server-and-lifecycle-contract)
when using its reference SDK profile, or supply a different explicit
`ConnectionProfile`. The application still owns networking, access policy,
trusted session admission, complete WebSocket message assembly and physical TX
retirement. UI write opt-in is not server permission. Exporting a header does not
qualify Ethernet, Wi-Fi, STM32 firmware or a board's Flash/RAM budget.

The existing host reference server lets you test the exported tree with the real
Model/dispatcher before selecting a firmware network stack. This stage neither
adds a third-party HTTP stack nor selects its license terms. SDK and UX license
notices remain in the exported `vendor/` tree.
